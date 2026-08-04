#include "meat2d/net/Discovery.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace meat2d::net {
namespace {
constexpr std::uint8_t server_flag_password = 1U << 0U;
constexpr std::uint8_t server_flag_nat_punch = 1U << 1U;
constexpr std::uint8_t valid_server_flags = server_flag_password | server_flag_nat_punch;

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

} // namespace meat2d::net
