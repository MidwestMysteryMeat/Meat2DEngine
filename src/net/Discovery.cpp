#include "meat2d/net/Discovery.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <limits>

namespace meat2d::net {
namespace {

constexpr std::uint8_t server_flag_password = 1U << 0U;
constexpr std::uint8_t server_flag_nat_punch = 1U << 1U;
constexpr std::uint8_t valid_server_flags = server_flag_password | server_flag_nat_punch;

std::uint64_t make_request_id() noexcept {
    static std::atomic_uint64_t counter{1};
    auto value =
        static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    value ^= counter.fetch_add(1, std::memory_order_relaxed) * 0x9E3779B185EBCA87ULL;
    value ^= value >> 30U;
    value *= 0xBF58476D1CE4E5B9ULL;
    value ^= value >> 27U;
    value *= 0x94D049BB133111EBULL;
    value ^= value >> 31U;
    return value == 0U ? 1U : value;
}

bool write_server_info(ByteWriter& writer, const ServerInfo& server) {
    const auto flags =
        static_cast<std::uint8_t>((server.password_protected ? server_flag_password : 0U) |
                                  (server.nat_punch_available ? server_flag_nat_punch : 0U));
    return valid_server_info(server) && writer.write_u64(server.server_id) &&
           writer.write_string(server.endpoint.address, maximum_endpoint_address_bytes) &&
           writer.write_u16(server.endpoint.port) && writer.write_u32(server.build_id) &&
           writer.write_u8(server.current_players) && writer.write_u8(server.maximum_clients) &&
           writer.write_u8(flags) && writer.write_string(server.name, maximum_server_name_bytes) &&
           writer.write_string(server.mode, maximum_mode_name_bytes) &&
           writer.write_string(server.map, maximum_map_name_bytes);
}

bool read_server_info(ByteReader& reader, ServerInfo& server) {
    std::uint8_t flags = 0;
    if (!reader.read_u64(server.server_id) ||
        !reader.read_string(server.endpoint.address, maximum_endpoint_address_bytes) ||
        !reader.read_u16(server.endpoint.port) || !reader.read_u32(server.build_id) ||
        !reader.read_u8(server.current_players) || !reader.read_u8(server.maximum_clients) ||
        !reader.read_u8(flags) || !reader.read_string(server.name, maximum_server_name_bytes) ||
        !reader.read_string(server.mode, maximum_mode_name_bytes) ||
        !reader.read_string(server.map, maximum_map_name_bytes) ||
        (flags & static_cast<std::uint8_t>(~valid_server_flags)) != 0U) {
        return false;
    }
    server.password_protected = (flags & server_flag_password) != 0U;
    server.nat_punch_available = (flags & server_flag_nat_punch) != 0U;
    return valid_server_info(server);
}

std::optional<std::vector<std::uint8_t>> control_datagram(PacketType type,
                                                          std::span<const std::uint8_t> payload) {
    PacketHeader header{};
    header.type = type;
    return encode_packet(header, payload);
}

bool same_server(const ServerInfo& left, const ServerInfo& right) {
    return left.server_id == right.server_id;
}

} // namespace

bool valid_server_info(const ServerInfo& server) noexcept {
    return server.server_id != 0U && server.endpoint.port != 0U &&
           !server.endpoint.address.empty() &&
           server.endpoint.address.size() <= maximum_endpoint_address_bytes &&
           !server.name.empty() && server.name.size() <= maximum_server_name_bytes &&
           !server.mode.empty() && server.mode.size() <= maximum_mode_name_bytes &&
           !server.map.empty() && server.map.size() <= maximum_map_name_bytes &&
           server.maximum_clients != 0U && server.maximum_clients <= maximum_players &&
           server.current_players <= server.maximum_clients;
}

std::vector<std::uint8_t> encode_lan_query(const LanQueryMessage& message) {
    ByteWriter writer;
    writer.write_u64(message.request_id);
    writer.write_u32(message.build_id);
    return writer.take();
}

std::optional<LanQueryMessage> decode_lan_query(std::span<const std::uint8_t> payload) {
    LanQueryMessage message{};
    ByteReader reader(payload);
    if (!reader.read_u64(message.request_id) || !reader.read_u32(message.build_id) ||
        !reader.empty() || message.request_id == 0U) {
        return std::nullopt;
    }
    return message;
}

std::optional<std::vector<std::uint8_t>>
encode_lan_announcement(const LanAnnouncementMessage& message) {
    ByteWriter writer;
    if (message.request_id == 0U || !writer.write_u64(message.request_id) ||
        !write_server_info(writer, message.server)) {
        return std::nullopt;
    }
    return writer.take();
}

std::optional<LanAnnouncementMessage>
decode_lan_announcement(std::span<const std::uint8_t> payload) {
    LanAnnouncementMessage message{};
    ByteReader reader(payload);
    if (!reader.read_u64(message.request_id) || !read_server_info(reader, message.server) ||
        !reader.empty() || message.request_id == 0U) {
        return std::nullopt;
    }
    return message;
}

std::optional<std::vector<std::uint8_t>>
encode_directory_registration(const DirectoryRegistrationMessage& message) {
    ByteWriter writer;
    if (message.registration_secret == 0U || !writer.write_u64(message.registration_secret) ||
        !write_server_info(writer, message.server)) {
        return std::nullopt;
    }
    return writer.take();
}

std::optional<DirectoryRegistrationMessage>
decode_directory_registration(std::span<const std::uint8_t> payload) {
    DirectoryRegistrationMessage message{};
    ByteReader reader(payload);
    if (!reader.read_u64(message.registration_secret) ||
        !read_server_info(reader, message.server) || !reader.empty() ||
        message.registration_secret == 0U) {
        return std::nullopt;
    }
    return message;
}

std::vector<std::uint8_t>
encode_directory_list_request(const DirectoryListRequestMessage& message) {
    ByteWriter writer;
    writer.write_u64(message.request_id);
    writer.write_u32(message.cursor);
    writer.write_u32(message.build_id);
    return writer.take();
}

std::optional<DirectoryListRequestMessage>
decode_directory_list_request(std::span<const std::uint8_t> payload) {
    DirectoryListRequestMessage message{};
    ByteReader reader(payload);
    if (!reader.read_u64(message.request_id) || !reader.read_u32(message.cursor) ||
        !reader.read_u32(message.build_id) || !reader.empty() || message.request_id == 0U ||
        message.cursor == directory_end_cursor) {
        return std::nullopt;
    }
    return message;
}

std::optional<std::vector<std::uint8_t>>
encode_directory_list_response(const DirectoryListResponseMessage& message) {
    if (message.request_id == 0U || message.servers.size() > maximum_directory_page_entries ||
        message.servers.size() > std::numeric_limits<std::uint8_t>::max()) {
        return std::nullopt;
    }
    ByteWriter writer;
    if (!writer.write_u64(message.request_id) || !writer.write_u32(message.next_cursor) ||
        !writer.write_u8(static_cast<std::uint8_t>(message.servers.size()))) {
        return std::nullopt;
    }
    for (const auto& server : message.servers) {
        if (!write_server_info(writer, server)) {
            return std::nullopt;
        }
    }
    return writer.take();
}

std::optional<DirectoryListResponseMessage>
decode_directory_list_response(std::span<const std::uint8_t> payload) {
    DirectoryListResponseMessage message{};
    std::uint8_t count = 0;
    ByteReader reader(payload);
    if (!reader.read_u64(message.request_id) || !reader.read_u32(message.next_cursor) ||
        !reader.read_u8(count) || count > maximum_directory_page_entries ||
        message.request_id == 0U) {
        return std::nullopt;
    }
    message.servers.reserve(count);
    for (std::uint8_t index = 0; index < count; ++index) {
        ServerInfo server{};
        if (!read_server_info(reader, server)) {
            return std::nullopt;
        }
        message.servers.push_back(std::move(server));
    }
    if (!reader.empty()) {
        return std::nullopt;
    }
    return message;
}

std::vector<std::uint8_t>
encode_directory_join_request(const DirectoryJoinRequestMessage& message) {
    ByteWriter writer;
    writer.write_u64(message.request_id);
    writer.write_u64(message.server_id);
    return writer.take();
}

std::optional<DirectoryJoinRequestMessage>
decode_directory_join_request(std::span<const std::uint8_t> payload) {
    DirectoryJoinRequestMessage message{};
    ByteReader reader(payload);
    if (!reader.read_u64(message.request_id) || !reader.read_u64(message.server_id) ||
        !reader.empty() || message.request_id == 0U || message.server_id == 0U) {
        return std::nullopt;
    }
    return message;
}

std::optional<std::vector<std::uint8_t>>
encode_directory_punch(const DirectoryPunchMessage& message) {
    ByteWriter writer;
    if (message.request_id == 0U || message.server_id == 0U || message.peer.port == 0U ||
        message.peer.address.empty() ||
        message.peer.address.size() > maximum_endpoint_address_bytes ||
        !writer.write_u64(message.request_id) || !writer.write_u64(message.server_id) ||
        !writer.write_string(message.peer.address, maximum_endpoint_address_bytes) ||
        !writer.write_u16(message.peer.port)) {
        return std::nullopt;
    }
    return writer.take();
}

std::optional<DirectoryPunchMessage> decode_directory_punch(std::span<const std::uint8_t> payload) {
    DirectoryPunchMessage message{};
    ByteReader reader(payload);
    if (!reader.read_u64(message.request_id) || !reader.read_u64(message.server_id) ||
        !reader.read_string(message.peer.address, maximum_endpoint_address_bytes) ||
        !reader.read_u16(message.peer.port) || !reader.empty() || message.request_id == 0U ||
        message.server_id == 0U || message.peer.address.empty() || message.peer.port == 0U) {
        return std::nullopt;
    }
    return message;
}

std::vector<std::uint8_t> encode_hole_punch(const HolePunchMessage& message) {
    ByteWriter writer;
    writer.write_u64(message.request_id);
    writer.write_u64(message.server_id);
    return writer.take();
}

std::optional<HolePunchMessage> decode_hole_punch(std::span<const std::uint8_t> payload) {
    HolePunchMessage message{};
    ByteReader reader(payload);
    if (!reader.read_u64(message.request_id) || !reader.read_u64(message.server_id) ||
        !reader.empty() || message.request_id == 0U || message.server_id == 0U) {
        return std::nullopt;
    }
    return message;
}

bool LanServerAdvertiser::start(std::uint16_t discovery_port) {
    stop();
    if (!socket_.open(discovery_port)) {
        last_error_ = std::string(socket_.last_error());
        return false;
    }
    last_error_.clear();
    return true;
}

void LanServerAdvertiser::stop() noexcept {
    socket_.close();
}

std::uint32_t LanServerAdvertiser::update(const ServerInfo& server) {
    if (!running() || !valid_server_info(server)) {
        return 0;
    }
    std::uint32_t replies = 0;
    for (std::size_t count = 0; count < 256U; ++count) {
        auto datagram = socket_.receive();
        if (!datagram) {
            break;
        }
        const auto packet = decode_packet(datagram->bytes);
        if (!packet || packet->header.type != PacketType::LanQuery) {
            continue;
        }
        const auto query = decode_lan_query(packet->payload);
        if (!query || (query->build_id != 0U && query->build_id != server.build_id)) {
            continue;
        }
        auto advertised = server;
        advertised.endpoint.address = "0.0.0.0";
        const auto payload = encode_lan_announcement({
            .request_id = query->request_id,
            .server = std::move(advertised),
        });
        const auto encoded =
            payload ? control_datagram(PacketType::LanAnnouncement, *payload) : std::nullopt;
        if (encoded && socket_.send(datagram->sender, *encoded)) {
            ++replies;
        }
    }
    if (!socket_.last_error().empty()) {
        last_error_ = std::string(socket_.last_error());
    }
    return replies;
}

bool LanServerAdvertiser::running() const noexcept {
    return socket_.valid();
}

std::uint16_t LanServerAdvertiser::port() const noexcept {
    return socket_.local_port();
}

std::string_view LanServerAdvertiser::last_error() const noexcept {
    return last_error_;
}

bool LanServerBrowser::refresh(std::uint16_t discovery_port, std::uint32_t build_id) {
    stop();
    if (discovery_port == 0U || !socket_.open() || !socket_.enable_broadcast()) {
        last_error_ = std::string(socket_.last_error());
        stop();
        return false;
    }
    request_id_ = make_request_id();
    build_id_ = build_id;
    servers_.clear();
    const auto payload = encode_lan_query({
        .request_id = request_id_,
        .build_id = build_id_,
    });
    const auto datagram = control_datagram(PacketType::LanQuery, payload);
    if (!datagram) {
        last_error_ = "LAN query could not be encoded";
        stop();
        return false;
    }
    const bool broadcast_sent = socket_.send(
        {
            .address = "255.255.255.255",
            .port = discovery_port,
        },
        *datagram);
    const bool loopback_sent = socket_.send(
        {
            .address = "127.0.0.1",
            .port = discovery_port,
        },
        *datagram);
    if (!broadcast_sent && !loopback_sent) {
        last_error_ = std::string(socket_.last_error());
        stop();
        return false;
    }
    last_error_.clear();
    return true;
}

std::uint32_t LanServerBrowser::update() {
    std::uint32_t added = 0;
    for (std::size_t count = 0; count < 256U; ++count) {
        auto datagram = socket_.receive();
        if (!datagram) {
            break;
        }
        const auto packet = decode_packet(datagram->bytes);
        if (!packet || packet->header.type != PacketType::LanAnnouncement) {
            continue;
        }
        auto announcement = decode_lan_announcement(packet->payload);
        if (!announcement || announcement->request_id != request_id_ ||
            (build_id_ != 0U && announcement->server.build_id != build_id_)) {
            continue;
        }
        announcement->server.endpoint.address = datagram->sender.address;
        const auto found =
            std::find_if(servers_.begin(), servers_.end(), [&](const ServerInfo& existing) {
                return same_server(existing, announcement->server);
            });
        if (found == servers_.end()) {
            servers_.push_back(std::move(announcement->server));
            ++added;
        } else {
            *found = std::move(announcement->server);
        }
    }
    return added;
}

void LanServerBrowser::stop() noexcept {
    socket_.close();
}

bool LanServerBrowser::searching() const noexcept {
    return socket_.valid();
}

std::span<const ServerInfo> LanServerBrowser::servers() const noexcept {
    return servers_;
}

std::string_view LanServerBrowser::last_error() const noexcept {
    return last_error_;
}

bool PublicServerBrowser::refresh(Endpoint directory, std::uint32_t build_id) {
    stop();
    const auto resolved = resolve_endpoint(directory.address, directory.port);
    if (!resolved) {
        last_error_ = "could not resolve public directory endpoint";
        return false;
    }
    if (!socket_.open()) {
        last_error_ = std::string(socket_.last_error());
        return false;
    }
    directory_ = *resolved;
    build_id_ = build_id;
    current_cursor_ = 0;
    pages_received_ = 0;
    complete_ = false;
    servers_.clear();
    if (!request_page(0)) {
        stop();
        return false;
    }
    last_error_.clear();
    return true;
}

std::uint32_t PublicServerBrowser::update() {
    std::uint32_t added = 0;
    for (std::size_t count = 0; count < 256U; ++count) {
        auto datagram = socket_.receive();
        if (!datagram) {
            break;
        }
        if (datagram->sender != directory_) {
            continue;
        }
        const auto packet = decode_packet(datagram->bytes);
        if (!packet || packet->header.type != PacketType::DirectoryListResponse) {
            continue;
        }
        const auto response = decode_directory_list_response(packet->payload);
        if (!response || response->request_id != request_id_) {
            continue;
        }
        if (pages_received_ >= 128U) {
            complete_ = true;
            last_error_ = "directory pagination exceeded its safety limit";
            socket_.close();
            break;
        }
        ++pages_received_;
        for (const auto& server : response->servers) {
            if (build_id_ != 0U && server.build_id != build_id_) {
                continue;
            }
            const auto found =
                std::find_if(servers_.begin(), servers_.end(), [&](const ServerInfo& existing) {
                    return same_server(existing, server);
                });
            if (found == servers_.end()) {
                servers_.push_back(server);
                ++added;
            } else {
                *found = server;
            }
        }
        if (response->next_cursor == directory_end_cursor) {
            complete_ = true;
            socket_.close();
        } else if (response->next_cursor <= current_cursor_) {
            complete_ = true;
            last_error_ = "directory returned a non-advancing page cursor";
            socket_.close();
        } else if (!request_page(response->next_cursor)) {
            complete_ = true;
            socket_.close();
        }
    }
    if (searching() &&
        std::chrono::steady_clock::now() - last_request_ >= std::chrono::milliseconds(400)) {
        if (request_attempts_ >= 5U) {
            complete_ = true;
            last_error_ = "public directory did not answer";
            socket_.close();
        } else if (!send_current_page()) {
            complete_ = true;
            socket_.close();
        }
    }
    return added;
}

void PublicServerBrowser::stop() noexcept {
    socket_.close();
}

bool PublicServerBrowser::searching() const noexcept {
    return socket_.valid() && !complete_;
}

bool PublicServerBrowser::complete() const noexcept {
    return complete_;
}

std::span<const ServerInfo> PublicServerBrowser::servers() const noexcept {
    return servers_;
}

std::string_view PublicServerBrowser::last_error() const noexcept {
    return last_error_;
}

bool PublicServerBrowser::request_page(std::uint32_t cursor) {
    if (cursor == directory_end_cursor) {
        last_error_ = "directory page cursor is invalid";
        return false;
    }
    request_id_ = make_request_id();
    current_cursor_ = cursor;
    request_attempts_ = 0;
    return send_current_page();
}

bool PublicServerBrowser::send_current_page() {
    const auto payload = encode_directory_list_request({
        .request_id = request_id_,
        .cursor = current_cursor_,
        .build_id = build_id_,
    });
    const auto datagram = control_datagram(PacketType::DirectoryListRequest, payload);
    if (!datagram || !socket_.send(directory_, *datagram)) {
        last_error_ = datagram ? std::string(socket_.last_error())
                               : "directory list request could not be encoded";
        return false;
    }
    ++request_attempts_;
    last_request_ = std::chrono::steady_clock::now();
    return true;
}

PublicDirectoryServer::PublicDirectoryServer(DirectoryConfig config) : config_(config) {
    config_.maximum_servers = std::clamp<std::size_t>(config_.maximum_servers, 1U, 65'536U);
    config_.maximum_datagrams_per_update =
        std::clamp<std::size_t>(config_.maximum_datagrams_per_update, 1U, 4'096U);
    config_.lease_timeout = std::max(config_.lease_timeout, std::chrono::milliseconds(100));
    servers_.reserve(std::min<std::size_t>(config_.maximum_servers, 4'096U));
}

PublicDirectoryServer::~PublicDirectoryServer() = default;

bool PublicDirectoryServer::start() {
    stop();
    if (!socket_.open(config_.port)) {
        last_error_ = std::string(socket_.last_error());
        return false;
    }
    last_error_.clear();
    return true;
}

void PublicDirectoryServer::stop() noexcept {
    socket_.close();
    servers_.clear();
}

DirectoryUpdateStats PublicDirectoryServer::update() {
    DirectoryUpdateStats stats{};
    if (!running()) {
        return stats;
    }
    expire(stats);
    for (std::size_t count = 0; count < config_.maximum_datagrams_per_update; ++count) {
        auto datagram = socket_.receive();
        if (!datagram) {
            break;
        }
        ++stats.datagrams_received;
        const auto packet = decode_packet(datagram->bytes);
        if (!packet || packet->header.flags != PacketFlagNone) {
            ++stats.invalid_datagrams;
            continue;
        }
        switch (packet->header.type) {
        case PacketType::DirectoryRegister:
            handle_registration(datagram->sender, packet->payload, stats);
            break;
        case PacketType::DirectoryListRequest:
            handle_list_request(datagram->sender, packet->payload, stats);
            break;
        case PacketType::DirectoryJoinRequest:
            handle_join_request(datagram->sender, packet->payload, stats);
            break;
        default:
            ++stats.invalid_datagrams;
            break;
        }
    }
    return stats;
}

bool PublicDirectoryServer::running() const noexcept {
    return socket_.valid();
}

std::uint16_t PublicDirectoryServer::port() const noexcept {
    return socket_.local_port();
}

std::size_t PublicDirectoryServer::server_count() const noexcept {
    return servers_.size();
}

std::string_view PublicDirectoryServer::last_error() const noexcept {
    return last_error_;
}

void PublicDirectoryServer::expire(DirectoryUpdateStats& stats) {
    const auto now = std::chrono::steady_clock::now();
    const auto previous = servers_.size();
    servers_.erase(std::remove_if(servers_.begin(), servers_.end(),
                                  [&](const RegisteredServer& server) {
                                      return now - server.last_seen > config_.lease_timeout;
                                  }),
                   servers_.end());
    stats.expired_servers = static_cast<std::uint32_t>(std::min<std::size_t>(
        previous - servers_.size(), std::numeric_limits<std::uint32_t>::max()));
}

void PublicDirectoryServer::handle_registration(const Endpoint& sender,
                                                std::span<const std::uint8_t> payload,
                                                DirectoryUpdateStats& stats) {
    auto registration = decode_directory_registration(payload);
    if (!registration) {
        ++stats.invalid_datagrams;
        return;
    }
    auto found =
        std::find_if(servers_.begin(), servers_.end(), [&](const RegisteredServer& existing) {
            return existing.server.server_id == registration->server.server_id;
        });
    if (found != servers_.end() &&
        found->registration_secret != registration->registration_secret) {
        ++stats.invalid_datagrams;
        return;
    }
    registration->server.endpoint = sender;
    registration->server.nat_punch_available = true;
    if (found == servers_.end()) {
        if (servers_.size() >= config_.maximum_servers) {
            ++stats.invalid_datagrams;
            return;
        }
        servers_.push_back({
            .server = std::move(registration->server),
            .registration_secret = registration->registration_secret,
            .last_seen = std::chrono::steady_clock::now(),
        });
    } else {
        found->server = std::move(registration->server);
        found->last_seen = std::chrono::steady_clock::now();
    }
    ++stats.registrations;
}

void PublicDirectoryServer::handle_list_request(const Endpoint& sender,
                                                std::span<const std::uint8_t> payload,
                                                DirectoryUpdateStats& stats) {
    const auto request = decode_directory_list_request(payload);
    if (!request) {
        ++stats.invalid_datagrams;
        return;
    }
    std::vector<const ServerInfo*> matches;
    matches.reserve(servers_.size());
    for (const auto& registered : servers_) {
        if (request->build_id == 0U || registered.server.build_id == request->build_id) {
            matches.push_back(&registered.server);
        }
    }
    std::sort(matches.begin(), matches.end(), [](const ServerInfo* left, const ServerInfo* right) {
        return left->server_id < right->server_id;
    });

    const auto start = std::min<std::size_t>(request->cursor, matches.size());
    const auto end = std::min(start + maximum_directory_page_entries, matches.size());
    DirectoryListResponseMessage response{
        .request_id = request->request_id,
        .next_cursor =
            end < matches.size() ? static_cast<std::uint32_t>(end) : directory_end_cursor,
        .servers = {},
    };
    response.servers.reserve(end - start);
    for (auto index = start; index < end; ++index) {
        response.servers.push_back(*matches[index]);
    }
    const auto response_payload = encode_directory_list_response(response);
    if (!response_payload ||
        !send_message(sender, PacketType::DirectoryListResponse, *response_payload, stats)) {
        ++stats.invalid_datagrams;
        return;
    }
    ++stats.list_requests;
}

void PublicDirectoryServer::handle_join_request(const Endpoint& sender,
                                                std::span<const std::uint8_t> payload,
                                                DirectoryUpdateStats& stats) {
    const auto request = decode_directory_join_request(payload);
    if (!request) {
        ++stats.invalid_datagrams;
        return;
    }
    const auto found =
        std::find_if(servers_.begin(), servers_.end(), [&](const RegisteredServer& server) {
            return server.server.server_id == request->server_id;
        });
    if (found == servers_.end()) {
        ++stats.invalid_datagrams;
        return;
    }
    const auto to_server = encode_directory_punch({
        .request_id = request->request_id,
        .server_id = request->server_id,
        .peer = sender,
    });
    const auto to_client = encode_directory_punch({
        .request_id = request->request_id,
        .server_id = request->server_id,
        .peer = found->server.endpoint,
    });
    if (!to_server || !to_client ||
        !send_message(found->server.endpoint, PacketType::DirectoryPunch, *to_server, stats) ||
        !send_message(sender, PacketType::DirectoryPunch, *to_client, stats)) {
        ++stats.invalid_datagrams;
        return;
    }
    ++stats.join_requests;
}

bool PublicDirectoryServer::send_message(const Endpoint& endpoint, PacketType type,
                                         std::span<const std::uint8_t> payload,
                                         DirectoryUpdateStats& stats) {
    const auto datagram = control_datagram(type, payload);
    if (!datagram || !socket_.send(endpoint, *datagram)) {
        last_error_ =
            datagram ? std::string(socket_.last_error()) : "directory message could not be encoded";
        return false;
    }
    ++stats.datagrams_sent;
    return true;
}

} // namespace meat2d::net
