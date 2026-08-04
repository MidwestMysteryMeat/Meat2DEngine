#include "meat2d/net/Session.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <limits>
#include <random>
#include <utility>

namespace meat2d::net {
namespace {

std::uint64_t make_nonce() noexcept {
    static const std::uint64_t process_entropy = []() noexcept {
        auto value =
            static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
        try {
            std::random_device random;
            value ^= static_cast<std::uint64_t>(random()) << 32U;
            value ^= static_cast<std::uint64_t>(random());
        } catch (...) {
        }
        return value | 1U;
    }();
    static std::atomic_uint64_t counter{1};
    auto value =
        static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    value ^= process_entropy;
    value ^= counter.fetch_add(1, std::memory_order_relaxed) * 0x9E3779B185EBCA87ULL;
    value ^= value >> 30U;
    value *= 0xBF58476D1CE4E5B9ULL;
    value ^= value >> 27U;
    value *= 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

} // namespace

AuthoritativeClient::AuthoritativeClient() = default;

AuthoritativeClient::~AuthoritativeClient() {
    disconnect();
}

bool AuthoritativeClient::connect(Endpoint server, std::string player_name, std::uint64_t nonce) {
    if (server.port == 0U || player_name.empty() ||
        player_name.size() > maximum_player_name_bytes) {
        last_error_ = "invalid connection settings";
        return false;
    }
    const auto resolved = resolve_endpoint(server.address, server.port);
    if (!resolved) {
        last_error_ = "could not resolve server endpoint";
        return false;
    }
    if (!begin_connection(std::move(player_name), nonce)) {
        return false;
    }

    server_ = *resolved;
    if (!send_hello()) {
        socket_.close();
        state_ = ClientConnectionState::Disconnected;
        return false;
    }
    return true;
}

bool AuthoritativeClient::connect_via_directory(Endpoint directory, std::uint64_t server_id,
                                                std::string player_name, std::uint64_t nonce) {
    if (directory.port == 0U || server_id == 0U || player_name.empty() ||
        player_name.size() > maximum_player_name_bytes) {
        last_error_ = "invalid directory connection settings";
        return false;
    }
    const auto resolved = resolve_endpoint(directory.address, directory.port);
    if (!resolved) {
        last_error_ = "could not resolve public directory endpoint";
        return false;
    }
    if (!begin_connection(std::move(player_name), nonce)) {
        return false;
    }
    directory_ = *resolved;
    requested_server_id_ = server_id;
    directory_request_id_ = (nonce_ ^ server_id ^ 0xD1AEC70A5E5510A1ULL) | 1U;
    if (!send_directory_join()) {
        socket_.close();
        state_ = ClientConnectionState::Disconnected;
        return false;
    }
    return true;
}

bool AuthoritativeClient::begin_connection(std::string player_name, std::uint64_t nonce) {
    disconnect();
    if (!socket_.open()) {
        last_error_ = std::string(socket_.last_error());
        return false;
    }
    server_ = {};
    directory_.reset();
    player_name_ = std::move(player_name);
    nonce_ = nonce == 0U ? make_nonce() : nonce;
    requested_server_id_ = 0;
    directory_request_id_ = 0;
    channel_ = ReliableChannel{};
    fragments_ = FragmentAssembler{};
    state_ = ClientConnectionState::Connecting;
    network_update_ = 0;
    next_input_sequence_ = 1;
    last_server_update_ = 0;
    needs_ack_ = false;
    welcome_.reset();
    latest_snapshot_.reset();
    replicated_world_.reset();
    replicated_scene_.reset();
    chunk_revisions_.clear();
    last_error_.clear();
    return true;
}

bool AuthoritativeClient::send_hello() {
    if (server_.port == 0U) {
        last_error_ = "server endpoint is not ready";
        return false;
    }
    const auto hello = encode_hello({
        .client_nonce = nonce_,
        .build_id = build_id_,
        .player_name = player_name_,
    });
    if (!hello) {
        last_error_ = "hello payload could not be encoded";
        return false;
    }
    const auto packet = channel_.make_packet(PacketType::Hello, *hello, network_update_, 0, true);
    return send_packet(packet, nullptr);
}

bool AuthoritativeClient::send_directory_join() {
    if (!directory_ || requested_server_id_ == 0U || directory_request_id_ == 0U) {
        last_error_ = "directory join state is incomplete";
        return false;
    }
    const auto payload = encode_directory_join_request({
        .request_id = directory_request_id_,
        .server_id = requested_server_id_,
    });
    PacketHeader header{};
    header.type = PacketType::DirectoryJoinRequest;
    const auto datagram = encode_packet(header, payload);
    if (!datagram || !socket_.send(*directory_, *datagram)) {
        last_error_ = datagram ? std::string(socket_.last_error())
                               : "directory join request could not be encoded";
        return false;
    }
    return true;
}

void AuthoritativeClient::disconnect() {
    if (socket_.valid() && state_ == ClientConnectionState::Connected) {
        const auto packet =
            channel_.make_packet(PacketType::Disconnect, {}, network_update_,
                                 latest_snapshot_ ? latest_snapshot_->server_tick : 0U, false);
        send_packet(packet, nullptr);
    }
    socket_.close();
    state_ = ClientConnectionState::Disconnected;
    directory_.reset();
    requested_server_id_ = 0;
    directory_request_id_ = 0;
}

ClientUpdateStats AuthoritativeClient::update() {
    ClientUpdateStats stats{};
    if (!socket_.valid()) {
        return stats;
    }
    ++network_update_;
    poll_datagrams(stats);
    flush_channel(stats);
    if (state_ == ClientConnectionState::Connecting && directory_ && server_.port == 0U &&
        network_update_ % 60U == 0U) {
        if (send_directory_join()) {
            ++stats.datagrams_sent;
        }
    }
    if (connected() && network_update_ % 60U == 0U) {
        const auto server_tick =
            latest_snapshot_ ? latest_snapshot_->server_tick : welcome_->server_tick;
        const auto heartbeat = channel_.make_acknowledgement(network_update_, server_tick);
        send_packet(heartbeat, &stats);
    }
    fragments_.expire(network_update_);
    std::erase_if(predicted_paints_, [this](const PredictedPaint& prediction) {
        return network_update_ - prediction.created_update > 600U;
    });
    if (state_ == ClientConnectionState::Connected &&
        network_update_ - last_server_update_ > 600U) {
        state_ = ClientConnectionState::TimedOut;
        socket_.close();
        last_error_ = "server timed out";
    }
    if (state_ == ClientConnectionState::Connecting && network_update_ > 600U) {
        state_ = ClientConnectionState::TimedOut;
        socket_.close();
        last_error_ =
            server_.port == 0U ? "directory join timed out" : "server handshake timed out";
    }
    return stats;
}

bool AuthoritativeClient::send_input(InputMessage input) {
    if (!connected()) {
        return false;
    }
    input.session_token = welcome_->session_token;
    input.input_sequence = next_input_sequence_++;
    if (input.target_tick == 0U) {
        input.target_tick = next_target_tick();
    }
    const auto payload = encode_input(input);
    const auto packet = channel_.make_packet(
        PacketType::Input, payload, network_update_,
        latest_snapshot_ ? latest_snapshot_->server_tick : input.target_tick, true);
    return send_packet(packet, nullptr);
}

bool AuthoritativeClient::set_focus(Vec2i focus) {
    return send_input({
        .kind = InputKind::SetFocus,
        .focus = focus,
        .target = focus,
    });
}

bool AuthoritativeClient::paint(Vec2i target, MaterialId material, std::uint8_t radius) {
    const auto input_sequence = next_input_sequence_;
    if (!send_input({
            .kind = InputKind::Paint,
            .focus = target,
            .target = target,
            .material = material,
            .radius = radius,
        })) {
        return false;
    }
    if (replicated_world_ != nullptr && replicated_world_->in_bounds(target)) {
        replicated_world_->paint_disc(target, radius, material);
        predicted_paints_.push_back({
            .input_sequence = input_sequence,
            .created_update = network_update_,
            .target = target,
            .material = material,
            .radius = radius,
        });
    }
    return true;
}

ClientConnectionState AuthoritativeClient::state() const noexcept {
    return state_;
}

bool AuthoritativeClient::connected() const noexcept {
    return state_ == ClientConnectionState::Connected;
}

std::uint8_t AuthoritativeClient::client_id() const noexcept {
    return welcome_ ? welcome_->client_id : 0U;
}

std::uint16_t AuthoritativeClient::local_port() const noexcept {
    return socket_.local_port();
}

const std::optional<WelcomeMessage>& AuthoritativeClient::welcome() const noexcept {
    return welcome_;
}

const std::optional<SnapshotMessage>& AuthoritativeClient::latest_snapshot() const noexcept {
    return latest_snapshot_;
}

const World* AuthoritativeClient::replicated_world() const noexcept {
    return replicated_world_.get();
}

World* AuthoritativeClient::replicated_world() noexcept {
    return replicated_world_.get();
}

const scene::Scene* AuthoritativeClient::replicated_scene() const noexcept {
    return replicated_scene_.get();
}

scene::Scene* AuthoritativeClient::replicated_scene() noexcept {
    return replicated_scene_.get();
}

std::size_t AuthoritativeClient::pending_predictions() const noexcept {
    return predicted_paints_.size();
}

std::uint32_t AuthoritativeClient::acknowledged_input_sequence() const noexcept {
    return acknowledged_input_sequence_;
}

std::uint64_t AuthoritativeClient::chunk_hash_mismatches() const noexcept {
    return chunk_hash_mismatches_;
}

void AuthoritativeClient::drop_acknowledged_predictions(std::uint32_t acknowledged_sequence) {
    std::erase_if(predicted_paints_, [acknowledged_sequence](const PredictedPaint& prediction) {
        return !sequence_more_recent(prediction.input_sequence, acknowledged_sequence);
    });
}

std::uint32_t AuthoritativeClient::reapply_predictions(RectI chunk_rect) {
    if (replicated_world_ == nullptr || predicted_paints_.empty()) {
        return 0;
    }
    std::uint32_t reapplied = 0;
    for (const auto& prediction : predicted_paints_) {
        const auto radius = static_cast<std::int32_t>(prediction.radius);
        const RectI disc_bounds{
            prediction.target.x - radius,
            prediction.target.y - radius,
            radius * 2 + 1,
            radius * 2 + 1,
        };
        const bool intersects = disc_bounds.x < chunk_rect.x + chunk_rect.width &&
                                chunk_rect.x < disc_bounds.x + disc_bounds.width &&
                                disc_bounds.y < chunk_rect.y + chunk_rect.height &&
                                chunk_rect.y < disc_bounds.y + disc_bounds.height;
        if (!intersects) {
            continue;
        }
        replicated_world_->paint_disc(prediction.target, radius, prediction.material);
        ++reapplied;
    }
    return reapplied;
}

std::string_view AuthoritativeClient::last_error() const noexcept {
    return last_error_;
}

void AuthoritativeClient::poll_datagrams(ClientUpdateStats& stats) {
    for (std::size_t count = 0; count < 256U; ++count) {
        auto datagram = socket_.receive();
        if (!datagram) {
            break;
        }
        if (directory_ && datagram->sender == *directory_) {
            ++stats.datagrams_received;
            const auto packet = decode_packet(datagram->bytes);
            const auto punch = packet && packet->header.type == PacketType::DirectoryPunch
                                   ? decode_directory_punch(packet->payload)
                                   : std::nullopt;
            if (!punch || punch->request_id != directory_request_id_ ||
                punch->server_id != requested_server_id_) {
                ++stats.invalid_datagrams;
                continue;
            }
            server_ = punch->peer;
            const auto payload = encode_hole_punch({
                .request_id = punch->request_id,
                .server_id = punch->server_id,
            });
            PacketHeader header{};
            header.type = PacketType::HolePunch;
            const auto encoded = encode_packet(header, payload);
            if (!encoded || !socket_.send(server_, *encoded) || !send_hello()) {
                ++stats.invalid_datagrams;
                continue;
            }
            stats.datagrams_sent += 2U;
            last_server_update_ = network_update_;
            continue;
        }
        if (datagram->sender != server_) {
            ++stats.invalid_datagrams;
            continue;
        }
        ++stats.datagrams_received;
        const auto packet = decode_packet(datagram->bytes);
        if (!packet) {
            ++stats.invalid_datagrams;
            continue;
        }
        if (packet->header.type == PacketType::HolePunch) {
            const auto punch = decode_hole_punch(packet->payload);
            if (!punch ||
                (requested_server_id_ != 0U && punch->server_id != requested_server_id_)) {
                ++stats.invalid_datagrams;
            }
            continue;
        }
        last_server_update_ = network_update_;
        if ((packet->header.flags & PacketFlagReliable) != 0U) {
            needs_ack_ = true;
        }
        if (!channel_.receive(packet->header)) {
            continue;
        }
        handle_packet(*packet, stats);
    }
}

void AuthoritativeClient::handle_packet(const Packet& packet, ClientUpdateStats& stats) {
    switch (packet.header.type) {
    case PacketType::Welcome: {
        const auto message = decode_welcome(packet.payload);
        if (!message || message->client_nonce != nonce_) {
            ++stats.invalid_datagrams;
            state_ = ClientConnectionState::Rejected;
            return;
        }
        welcome_ = message;
        state_ = ClientConnectionState::Connected;
        replicated_world_ = std::make_unique<World>(WorldConfig{
            .width = message->world_width,
            .height = message->world_height,
            .seed = message->world_seed,
            .sleep_after_ticks = 30,
        });
        chunk_revisions_.assign(replicated_world_->chunks().size(),
                                std::numeric_limits<std::uint64_t>::max());
        predicted_paints_.clear();
        acknowledged_input_sequence_ = 0;
        chunk_hash_mismatches_ = 0;
        break;
    }
    case PacketType::Snapshot:
        if (const auto message = decode_snapshot(packet.payload)) {
            latest_snapshot_ = message;
            if (sequence_more_recent(message->acknowledged_input_sequence,
                                     acknowledged_input_sequence_)) {
                acknowledged_input_sequence_ = message->acknowledged_input_sequence;
                drop_acknowledged_predictions(acknowledged_input_sequence_);
            }
        } else {
            ++stats.invalid_datagrams;
        }
        break;
    case PacketType::ChunkDelta:
        if ((packet.header.flags & PacketFlagFragment) == 0U || replicated_world_ == nullptr) {
            ++stats.invalid_datagrams;
            break;
        }
        if (const auto completed = fragments_.accept(packet.payload, network_update_)) {
            const auto applied = apply_chunk_delta(*replicated_world_, *completed);
            if (!applied) {
                ++stats.invalid_datagrams;
                break;
            }
            const auto index = static_cast<std::size_t>(applied->chunk_y) *
                                   static_cast<std::size_t>(replicated_world_->chunk_columns()) +
                               applied->chunk_x;
            if (index < chunk_revisions_.size()) {
                chunk_revisions_[index] = applied->revision;
            }
            if (replicated_world_->chunk_hash(index) != applied->chunk_hash) {
                ++stats.chunk_hash_mismatches;
                ++chunk_hash_mismatches_;
            }
            stats.reapplied_predictions += reapply_predictions({
                static_cast<std::int32_t>(applied->chunk_x) * chunk_size,
                static_cast<std::int32_t>(applied->chunk_y) * chunk_size,
                chunk_size,
                chunk_size,
            });
            ++stats.completed_chunks;
            stats.changed_cells += applied->changed_cells;
        }
        break;
    case PacketType::SceneSnapshot:
        if ((packet.header.flags & PacketFlagFragment) == 0U) {
            ++stats.invalid_datagrams;
            break;
        }
        if (const auto completed = fragments_.accept(packet.payload, network_update_)) {
            const auto message = decode_scene_snapshot(*completed);
            const auto decoded = message
                                     ? scene::decode_snapshot({
                                           .state_hash = message->state_hash,
                                           .bytes = message->bytes,
                                       })
                                     : std::optional<scene::Scene>{};
            if (!decoded) {
                ++stats.invalid_datagrams;
                break;
            }
            replicated_scene_ = std::make_unique<scene::Scene>(std::move(*decoded));
            ++stats.completed_scene_snapshots;
        }
        break;
    case PacketType::Disconnect:
        state_ = ClientConnectionState::Rejected;
        last_error_ = "server disconnected";
        socket_.close();
        break;
    case PacketType::Hello:
    case PacketType::Input:
    case PacketType::Acknowledgement:
    case PacketType::LanQuery:
    case PacketType::LanAnnouncement:
    case PacketType::DirectoryRegister:
    case PacketType::DirectoryListRequest:
    case PacketType::DirectoryListResponse:
    case PacketType::DirectoryJoinRequest:
    case PacketType::DirectoryPunch:
    case PacketType::HolePunch:
        break;
    }
}

bool AuthoritativeClient::send_packet(const Packet& packet, ClientUpdateStats* stats) {
    const auto datagram = encode_packet(packet.header, packet.payload);
    if (!datagram || !socket_.send(server_, *datagram)) {
        last_error_ = std::string(socket_.last_error());
        return false;
    }
    needs_ack_ = false;
    if (stats != nullptr) {
        ++stats->datagrams_sent;
    }
    return true;
}

void AuthoritativeClient::flush_channel(ClientUpdateStats& stats) {
    const auto server_tick = latest_snapshot_ ? latest_snapshot_->server_tick
                             : welcome_       ? welcome_->server_tick
                                              : 0U;
    for (const auto& packet : channel_.collect_retransmissions(network_update_, server_tick)) {
        send_packet(packet, &stats);
    }
    if (needs_ack_) {
        const auto acknowledgement = channel_.make_acknowledgement(network_update_, server_tick);
        send_packet(acknowledgement, &stats);
    }
}

std::uint32_t AuthoritativeClient::next_target_tick() const noexcept {
    if (latest_snapshot_) {
        return latest_snapshot_->server_tick + 2U;
    }
    return welcome_ ? welcome_->server_tick + 2U : 1U;
}

} // namespace meat2d::net
