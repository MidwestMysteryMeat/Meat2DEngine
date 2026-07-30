#include "meat2d/net/Session.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <limits>
#include <random>
#include <tuple>

namespace meat2d::net {
namespace {

std::uint32_t wire_tick(Tick tick) noexcept {
    return static_cast<std::uint32_t>(tick);
}

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

std::uint64_t make_session_token(std::uint64_t secret, std::uint64_t nonce,
                                 std::uint8_t client_id) noexcept {
    auto value = secret ^ nonce ^ (static_cast<std::uint64_t>(client_id) * 0x9E3779B185EBCA87ULL);
    value ^= value >> 30U;
    value *= 0xBF58476D1CE4E5B9ULL;
    value ^= value >> 27U;
    value *= 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

} // namespace

AuthoritativeServer::AuthoritativeServer(ServerConfig config)
    : config_(std::move(config)), simulation_(config_.world), server_secret_(make_nonce() | 1U),
      server_id_(make_nonce() | 1U), registration_secret_(make_nonce() | 1U) {
    config_.maximum_clients =
        std::clamp<std::uint8_t>(config_.maximum_clients, 1U, maximum_players);
    config_.maximum_brush_radius = std::clamp<std::uint8_t>(config_.maximum_brush_radius, 1U, 16U);
    config_.maximum_inputs_per_update =
        std::clamp<std::uint8_t>(config_.maximum_inputs_per_update, 1U, 16U);
    config_.snapshot_interval_ticks = std::max<std::uint32_t>(1U, config_.snapshot_interval_ticks);
    config_.chunk_interval_ticks = std::max<std::uint32_t>(1U, config_.chunk_interval_ticks);
    config_.directory_heartbeat_updates =
        std::max<std::uint32_t>(1U, config_.directory_heartbeat_updates);
    clients_.reserve(config_.maximum_clients);
    inputs_.reserve(256);
}

AuthoritativeServer::~AuthoritativeServer() = default;

bool AuthoritativeServer::start() {
    stop();
    if (!socket_.open(config_.port)) {
        last_error_ = std::string(socket_.last_error());
        return false;
    }
    if (!valid_server_info(server_info())) {
        last_error_ = "server listing metadata is invalid";
        stop();
        return false;
    }
    if (config_.advertise_lan && !lan_advertiser_.start(config_.lan_discovery_port)) {
        last_error_ = "LAN discovery failed: " + std::string(lan_advertiser_.last_error());
        stop();
        return false;
    }
    if (config_.advertise_public) {
        if (!config_.public_directory) {
            last_error_ = "public advertising requires a directory endpoint";
            stop();
            return false;
        }
        public_directory_ =
            resolve_endpoint(config_.public_directory->address, config_.public_directory->port);
        if (!public_directory_) {
            last_error_ = "could not resolve public directory endpoint";
            stop();
            return false;
        }
    }
    network_update_ = 0;
    last_error_.clear();
    return true;
}

void AuthoritativeServer::stop() noexcept {
    lan_advertiser_.stop();
    socket_.close();
    public_directory_.reset();
    clients_.clear();
    inputs_.clear();
}

bool AuthoritativeServer::running() const noexcept {
    return socket_.valid();
}

std::uint16_t AuthoritativeServer::port() const noexcept {
    return socket_.local_port();
}

std::size_t AuthoritativeServer::client_count() const noexcept {
    return clients_.size();
}

std::uint64_t AuthoritativeServer::server_id() const noexcept {
    return server_id_;
}

ServerInfo AuthoritativeServer::server_info() const {
    return {
        .server_id = server_id_,
        .endpoint =
            {
                .address = "0.0.0.0",
                .port = port(),
            },
        .name = config_.session_name,
        .mode = config_.mode_name,
        .map = config_.map_name,
        .build_id = config_.build_id,
        .current_players = static_cast<std::uint8_t>(clients_.size()),
        .maximum_clients = config_.maximum_clients,
        .password_protected = config_.password_protected,
        .nat_punch_available = config_.advertise_public,
    };
}

std::string_view AuthoritativeServer::last_error() const noexcept {
    return last_error_;
}

ai::LivingSimulation& AuthoritativeServer::simulation() noexcept {
    return simulation_;
}

const ai::LivingSimulation& AuthoritativeServer::simulation() const noexcept {
    return simulation_;
}

ServerUpdateStats AuthoritativeServer::update() {
    ServerUpdateStats stats{};
    if (!running()) {
        return stats;
    }
    ++network_update_;
    for (auto& client : clients_) {
        client.inputs_this_update = 0;
    }
    if (lan_advertiser_.running()) {
        stats.lan_replies = lan_advertiser_.update(server_info());
    }
    poll_datagrams(stats);
    apply_inputs(stats);
    stats.simulation = simulation_.step();
    send_world_updates(stats);
    flush_channels(stats);
    remove_timed_out_clients();
    if (public_directory_ &&
        (network_update_ == 1U || network_update_ % config_.directory_heartbeat_updates == 0U)) {
        send_directory_registration(stats);
    }
    stats.connected_clients = static_cast<std::uint32_t>(clients_.size());
    return stats;
}

AuthoritativeServer::ClientSlot*
AuthoritativeServer::find_client(const Endpoint& endpoint) noexcept {
    const auto found =
        std::find_if(clients_.begin(), clients_.end(),
                     [&](const ClientSlot& client) { return client.endpoint == endpoint; });
    return found == clients_.end() ? nullptr : &*found;
}

AuthoritativeServer::ClientSlot* AuthoritativeServer::find_client(std::uint8_t id) noexcept {
    const auto found = std::find_if(clients_.begin(), clients_.end(),
                                    [id](const ClientSlot& client) { return client.id == id; });
    return found == clients_.end() ? nullptr : &*found;
}

std::uint8_t AuthoritativeServer::allocate_client_id() const noexcept {
    for (std::uint8_t id = 1; id <= config_.maximum_clients; ++id) {
        const bool used = std::any_of(clients_.begin(), clients_.end(),
                                      [id](const ClientSlot& client) { return client.id == id; });
        if (!used) {
            return id;
        }
    }
    return 0;
}

void AuthoritativeServer::poll_datagrams(ServerUpdateStats& stats) {
    for (std::size_t count = 0; count < 256U; ++count) {
        auto datagram = socket_.receive();
        if (!datagram) {
            break;
        }
        ++stats.datagrams_received;
        const auto packet = decode_packet(datagram->bytes);
        if (!packet) {
            ++stats.invalid_datagrams;
            continue;
        }
        if (auto* client = find_client(datagram->sender)) {
            handle_client_packet(*client, *packet, stats);
        } else {
            handle_unknown(datagram->sender, *packet, stats);
        }
    }
}

void AuthoritativeServer::handle_unknown(const Endpoint& endpoint, const Packet& packet,
                                         ServerUpdateStats& stats) {
    if (packet.header.type == PacketType::DirectoryPunch) {
        const auto punch = decode_directory_punch(packet.payload);
        if (!public_directory_ || endpoint != *public_directory_ || !punch ||
            punch->server_id != server_id_) {
            ++stats.invalid_datagrams;
            return;
        }
        const auto payload = encode_hole_punch({
            .request_id = punch->request_id,
            .server_id = server_id_,
        });
        PacketHeader header{};
        header.type = PacketType::HolePunch;
        const auto datagram = encode_packet(header, payload);
        if (datagram && socket_.send(punch->peer, *datagram)) {
            ++stats.datagrams_sent;
            ++stats.nat_punches;
        } else {
            ++stats.invalid_datagrams;
        }
        return;
    }
    if (packet.header.type == PacketType::HolePunch) {
        const auto punch = decode_hole_punch(packet.payload);
        if (!punch || punch->server_id != server_id_) {
            ++stats.invalid_datagrams;
        }
        return;
    }
    if (packet.header.type != PacketType::Hello ||
        (packet.header.flags & PacketFlagReliable) == 0U) {
        ++stats.invalid_datagrams;
        return;
    }
    const auto hello = decode_hello(packet.payload);
    const auto id = allocate_client_id();
    if (!hello || id == 0U) {
        ++stats.invalid_datagrams;
        return;
    }

    clients_.emplace_back();
    auto& added = clients_.back();
    added.id = id;
    added.endpoint = endpoint;
    added.nonce = hello->client_nonce;
    added.session_token = make_session_token(server_secret_, added.nonce, added.id);
    added.name = hello->player_name;
    added.focus = {
        simulation_.world().width() / 2,
        simulation_.world().height() / 2,
    };
    added.known_chunk_revisions.assign(simulation_.world().chunks().size(),
                                       std::numeric_limits<std::uint64_t>::max());
    added.last_heard_update = network_update_;
    added.needs_ack = true;
    added.channel.receive(packet.header);
    const auto welcome = encode_welcome({
        .client_nonce = added.nonce,
        .session_token = added.session_token,
        .world_seed = simulation_.world().seed(),
        .server_tick = wire_tick(simulation_.world().current_tick()),
        .world_width = simulation_.world().width(),
        .world_height = simulation_.world().height(),
        .tick_rate = config_.tick_rate,
        .client_id = added.id,
        .maximum_clients = config_.maximum_clients,
    });
    send_message(added, PacketType::Welcome, welcome, true, PacketFlagNone, stats);
}

void AuthoritativeServer::handle_client_packet(ClientSlot& client, const Packet& packet,
                                               ServerUpdateStats& stats) {
    client.last_heard_update = network_update_;
    if ((packet.header.flags & PacketFlagReliable) != 0U) {
        client.needs_ack = true;
    }
    if (!client.channel.receive(packet.header)) {
        return;
    }

    if (packet.header.type == PacketType::Hello) {
        const auto hello = decode_hello(packet.payload);
        if (!hello || hello->client_nonce != client.nonce) {
            ++stats.invalid_datagrams;
            return;
        }
        const auto welcome = encode_welcome({
            .client_nonce = client.nonce,
            .session_token = client.session_token,
            .world_seed = simulation_.world().seed(),
            .server_tick = wire_tick(simulation_.world().current_tick()),
            .world_width = simulation_.world().width(),
            .world_height = simulation_.world().height(),
            .tick_rate = config_.tick_rate,
            .client_id = client.id,
            .maximum_clients = config_.maximum_clients,
        });
        send_message(client, PacketType::Welcome, welcome, true, PacketFlagNone, stats);
        return;
    }
    if (packet.header.type == PacketType::Input) {
        auto input = decode_input(packet.payload);
        if (!input || input->session_token != client.session_token || input->input_sequence == 0U ||
            client.inputs_this_update >= config_.maximum_inputs_per_update ||
            !sequence_more_recent(input->input_sequence, client.last_input_sequence)) {
            ++stats.rejected_inputs;
            return;
        }

        const auto current_tick = wire_tick(simulation_.world().current_tick());
        if (input->target_tick <= current_tick) {
            input->target_tick = current_tick + 1U;
        }
        if (input->target_tick - current_tick > 8U ||
            input->radius > config_.maximum_brush_radius) {
            ++stats.rejected_inputs;
            return;
        }
        ++client.inputs_this_update;
        client.last_input_sequence = input->input_sequence;
        client.focus = {
            std::clamp(input->focus.x, 0, simulation_.world().width() - 1),
            std::clamp(input->focus.y, 0, simulation_.world().height() - 1),
        };
        if (input->kind == InputKind::SetFocus) {
            ++stats.accepted_inputs;
            return;
        }
        if (!simulation_.world().in_bounds(input->target)) {
            ++stats.rejected_inputs;
            return;
        }
        inputs_.push_back({
            .client_id = client.id,
            .message = *input,
        });
        ++stats.accepted_inputs;
        return;
    }
    if (packet.header.type == PacketType::Disconnect) {
        client.last_heard_update = network_update_ - config_.client_timeout_updates - 1U;
    }
}

void AuthoritativeServer::apply_inputs(ServerUpdateStats& stats) {
    const auto target_tick = wire_tick(simulation_.world().current_tick() + 1U);
    std::stable_sort(
        inputs_.begin(), inputs_.end(), [](const QueuedInput& left, const QueuedInput& right) {
            return std::tie(left.message.target_tick, left.client_id, left.message.input_sequence) <
                   std::tie(right.message.target_tick, right.client_id,
                            right.message.input_sequence);
        });
    std::vector<QueuedInput> future;
    future.reserve(inputs_.size());
    for (const auto& queued : inputs_) {
        if (queued.message.target_tick > target_tick) {
            future.push_back(queued);
            continue;
        }
        if (queued.message.target_tick < target_tick || queued.message.kind != InputKind::Paint) {
            ++stats.rejected_inputs;
            continue;
        }

        const auto radius = static_cast<std::int32_t>(queued.message.radius);
        const auto radius_squared = radius * radius;
        for (std::int32_t y = queued.message.target.y - radius;
             y <= queued.message.target.y + radius; ++y) {
            for (std::int32_t x = queued.message.target.x - radius;
                 x <= queued.message.target.x + radius; ++x) {
                const auto dx = x - queued.message.target.x;
                const auto dy = y - queued.message.target.y;
                if (dx * dx + dy * dy > radius_squared) {
                    continue;
                }
                if (!simulation_.queue_command({
                        .target_tick = target_tick,
                        .issuer = network_issuer_base | queued.client_id,
                        .sequence = queued.message.input_sequence,
                        .type = ai::CommandType::Paint,
                        .target = {x, y},
                        .material = queued.message.material,
                    })) {
                    ++stats.rejected_inputs;
                }
            }
        }
    }
    inputs_ = std::move(future);
}

void AuthoritativeServer::send_world_updates(ServerUpdateStats& stats) {
    const auto current_tick = wire_tick(simulation_.world().current_tick());
    if (current_tick % config_.snapshot_interval_ticks == 0U) {
        const auto active_chunks = static_cast<std::uint16_t>(std::min<std::uint32_t>(
            stats.simulation.world.active_chunks, std::numeric_limits<std::uint16_t>::max()));
        const auto snapshot = encode_snapshot({
            .server_tick = current_tick,
            .state_hash = simulation_.state_hash(),
            .organism_population = simulation_.organisms().population(),
            .agent_count = static_cast<std::uint16_t>(std::min<std::size_t>(
                simulation_.agents().size(), std::numeric_limits<std::uint16_t>::max())),
            .active_chunks = active_chunks,
        });
        for (auto& client : clients_) {
            send_message(client, PacketType::Snapshot, snapshot, false, PacketFlagNone, stats);
        }
    }

    if (current_tick % config_.chunk_interval_ticks != 0U) {
        return;
    }
    if (current_tick % 600U == 0U) {
        for (auto& client : clients_) {
            std::fill(client.known_chunk_revisions.begin(), client.known_chunk_revisions.end(),
                      std::numeric_limits<std::uint64_t>::max());
        }
    }

    const auto chunks = simulation_.world().chunks();
    for (auto& client : clients_) {
        const auto interest =
            interested_chunks(simulation_.world(), client.focus, config_.interest_radius_chunks);
        for (const auto chunk_index : interest) {
            if (chunk_index >= client.known_chunk_revisions.size() ||
                client.known_chunk_revisions[chunk_index] == chunks[chunk_index].revision) {
                continue;
            }
            send_chunk(client, chunk_index, stats);
            client.known_chunk_revisions[chunk_index] = chunks[chunk_index].revision;
            break;
        }
    }
}

bool AuthoritativeServer::send_packet(ClientSlot& client, const Packet& packet,
                                      ServerUpdateStats& stats) {
    const auto datagram = encode_packet(packet.header, packet.payload);
    if (!datagram || !socket_.send(client.endpoint, *datagram)) {
        last_error_ = std::string(socket_.last_error());
        return false;
    }
    client.needs_ack = false;
    ++stats.datagrams_sent;
    return true;
}

void AuthoritativeServer::send_message(ClientSlot& client, PacketType type,
                                       std::span<const std::uint8_t> payload, bool reliable,
                                       std::uint8_t additional_flags, ServerUpdateStats& stats) {
    const auto packet = client.channel.make_packet(type, payload, network_update_,
                                                   wire_tick(simulation_.world().current_tick()),
                                                   reliable, additional_flags);
    send_packet(client, packet, stats);
}

void AuthoritativeServer::send_chunk(ClientSlot& client, std::size_t chunk_index,
                                     ServerUpdateStats& stats) {
    const auto encoded = encode_chunk_delta(simulation_.world(), chunk_index);
    if (!encoded) {
        return;
    }
    const auto fragments = fragment_payload(client.next_message_id++, *encoded);
    if (fragments.empty()) {
        return;
    }
    for (const auto& fragment : fragments) {
        send_message(client, PacketType::ChunkDelta, fragment, true, PacketFlagFragment, stats);
    }
    ++stats.chunk_messages;
}

void AuthoritativeServer::flush_channels(ServerUpdateStats& stats) {
    for (auto& client : clients_) {
        for (const auto& packet : client.channel.collect_retransmissions(
                 network_update_, wire_tick(simulation_.world().current_tick()))) {
            send_packet(client, packet, stats);
        }
        if (client.needs_ack) {
            const auto acknowledgement = client.channel.make_acknowledgement(
                network_update_, wire_tick(simulation_.world().current_tick()));
            send_packet(client, acknowledgement, stats);
        }
    }
}

void AuthoritativeServer::remove_timed_out_clients() {
    clients_.erase(std::remove_if(clients_.begin(), clients_.end(),
                                  [&](const ClientSlot& client) {
                                      return network_update_ - client.last_heard_update >
                                             config_.client_timeout_updates;
                                  }),
                   clients_.end());
}

void AuthoritativeServer::send_directory_registration(ServerUpdateStats& stats) {
    if (!public_directory_) {
        return;
    }
    const auto payload = encode_directory_registration({
        .registration_secret = registration_secret_,
        .server = server_info(),
    });
    if (!payload) {
        last_error_ = "public server registration could not be encoded";
        return;
    }
    PacketHeader header{};
    header.type = PacketType::DirectoryRegister;
    const auto datagram = encode_packet(header, *payload);
    if (!datagram || !socket_.send(*public_directory_, *datagram)) {
        last_error_ = datagram ? std::string(socket_.last_error())
                               : "public server registration exceeded UDP limits";
        return;
    }
    ++stats.datagrams_sent;
    ++stats.directory_heartbeats;
}

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
    return send_input({
        .kind = InputKind::Paint,
        .focus = target,
        .target = target,
        .material = material,
        .radius = radius,
    });
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
        break;
    }
    case PacketType::Snapshot:
        if (const auto message = decode_snapshot(packet.payload)) {
            latest_snapshot_ = message;
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
            ++stats.completed_chunks;
            stats.changed_cells += applied->changed_cells;
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
