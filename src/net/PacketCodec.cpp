#include "meat2d/net/PacketCodec.hpp"

#include <algorithm>
#include <limits>

namespace meat2d::net {

ByteWriter::ByteWriter(std::size_t maximum_bytes) : maximum_bytes_(maximum_bytes) {
    bytes_.reserve(std::min(maximum_bytes, maximum_datagram_bytes));
}

bool ByteWriter::write_u8(std::uint8_t value) {
    if (!reserve_for(1)) {
        return false;
    }
    bytes_.push_back(value);
    return true;
}

bool ByteWriter::write_u16(std::uint16_t value) {
    return write_u8(static_cast<std::uint8_t>(value)) &&
           write_u8(static_cast<std::uint8_t>(value >> 8U));
}

bool ByteWriter::write_u32(std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        if (!write_u8(static_cast<std::uint8_t>(value >> shift))) {
            return false;
        }
    }
    return true;
}

bool ByteWriter::write_u64(std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        if (!write_u8(static_cast<std::uint8_t>(value >> shift))) {
            return false;
        }
    }
    return true;
}

bool ByteWriter::write_i16(std::int16_t value) {
    return write_u16(static_cast<std::uint16_t>(value));
}

bool ByteWriter::write_i32(std::int32_t value) {
    return write_u32(static_cast<std::uint32_t>(value));
}

bool ByteWriter::write_bytes(std::span<const std::uint8_t> bytes) {
    if (!reserve_for(bytes.size())) {
        return false;
    }
    bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
    return true;
}

bool ByteWriter::write_string(std::string_view text, std::size_t maximum_length) {
    if (text.size() > maximum_length || text.size() > std::numeric_limits<std::uint8_t>::max()) {
        return false;
    }
    return write_u8(static_cast<std::uint8_t>(text.size())) &&
           write_bytes({
               reinterpret_cast<const std::uint8_t*>(text.data()),
               text.size(),
           });
}

std::span<const std::uint8_t> ByteWriter::bytes() const noexcept {
    return bytes_;
}

std::vector<std::uint8_t> ByteWriter::take() noexcept {
    return std::move(bytes_);
}

bool ByteWriter::reserve_for(std::size_t bytes) const noexcept {
    return bytes <= maximum_bytes_ && bytes_.size() <= maximum_bytes_ - bytes;
}

ByteReader::ByteReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

bool ByteReader::read_u8(std::uint8_t& value) {
    if (remaining() < 1U) {
        return false;
    }
    value = bytes_[offset_++];
    return true;
}

bool ByteReader::read_u16(std::uint16_t& value) {
    std::uint8_t low = 0;
    std::uint8_t high = 0;
    if (!read_u8(low) || !read_u8(high)) {
        return false;
    }
    value = static_cast<std::uint16_t>(static_cast<std::uint16_t>(low) |
                                       (static_cast<std::uint16_t>(high) << 8U));
    return true;
}

bool ByteReader::read_u32(std::uint32_t& value) {
    value = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        std::uint8_t byte = 0;
        if (!read_u8(byte)) {
            return false;
        }
        value |= static_cast<std::uint32_t>(byte) << shift;
    }
    return true;
}

bool ByteReader::read_u64(std::uint64_t& value) {
    value = 0;
    for (int shift = 0; shift < 64; shift += 8) {
        std::uint8_t byte = 0;
        if (!read_u8(byte)) {
            return false;
        }
        value |= static_cast<std::uint64_t>(byte) << shift;
    }
    return true;
}

bool ByteReader::read_i16(std::int16_t& value) {
    std::uint16_t encoded = 0;
    if (!read_u16(encoded)) {
        return false;
    }
    value = static_cast<std::int16_t>(encoded);
    return true;
}

bool ByteReader::read_i32(std::int32_t& value) {
    std::uint32_t encoded = 0;
    if (!read_u32(encoded)) {
        return false;
    }
    value = static_cast<std::int32_t>(encoded);
    return true;
}

bool ByteReader::read_bytes(std::size_t count, std::span<const std::uint8_t>& value) {
    if (count > remaining()) {
        return false;
    }
    value = bytes_.subspan(offset_, count);
    offset_ += count;
    return true;
}

bool ByteReader::read_string(std::string& value, std::size_t maximum_length) {
    std::uint8_t length = 0;
    if (!read_u8(length) || length > maximum_length || length > remaining()) {
        return false;
    }
    const auto text = bytes_.subspan(offset_, length);
    value.assign(reinterpret_cast<const char*>(text.data()), text.size());
    offset_ += length;
    return true;
}

std::size_t ByteReader::remaining() const noexcept {
    return bytes_.size() - offset_;
}

bool ByteReader::empty() const noexcept {
    return remaining() == 0U;
}

std::optional<std::vector<std::uint8_t>> encode_packet(PacketHeader header,
                                                       std::span<const std::uint8_t> payload) {
    if (payload.size() > maximum_datagram_bytes - encoded_header_bytes ||
        payload.size() > std::numeric_limits<std::uint16_t>::max()) {
        return std::nullopt;
    }

    header.magic = protocol_magic;
    header.version = protocol_version;
    header.payload_bytes = static_cast<std::uint16_t>(payload.size());
    header.reserved = 0;
    ByteWriter writer(maximum_datagram_bytes);
    const bool written =
        writer.write_u32(header.magic) && writer.write_u16(header.version) &&
        writer.write_u8(static_cast<std::uint8_t>(header.type)) && writer.write_u8(header.flags) &&
        writer.write_u32(header.sequence) && writer.write_u32(header.acknowledgement) &&
        writer.write_u32(header.acknowledgement_bits) && writer.write_u32(header.server_tick) &&
        writer.write_u16(header.payload_bytes) && writer.write_u16(header.reserved) &&
        writer.write_bytes(payload);
    return written ? std::optional(writer.take()) : std::nullopt;
}

std::optional<Packet> decode_packet(std::span<const std::uint8_t> datagram) {
    if (datagram.size() < encoded_header_bytes || datagram.size() > maximum_datagram_bytes) {
        return std::nullopt;
    }

    Packet packet{};
    std::uint8_t type = 0;
    ByteReader reader(datagram);
    if (!reader.read_u32(packet.header.magic) || !reader.read_u16(packet.header.version) ||
        !reader.read_u8(type) || !reader.read_u8(packet.header.flags) ||
        !reader.read_u32(packet.header.sequence) ||
        !reader.read_u32(packet.header.acknowledgement) ||
        !reader.read_u32(packet.header.acknowledgement_bits) ||
        !reader.read_u32(packet.header.server_tick) ||
        !reader.read_u16(packet.header.payload_bytes) || !reader.read_u16(packet.header.reserved)) {
        return std::nullopt;
    }

    if (packet.header.magic != protocol_magic || packet.header.version != protocol_version ||
        type > static_cast<std::uint8_t>(PacketType::HolePunch) ||
        (packet.header.flags &
         static_cast<std::uint8_t>(~(PacketFlagReliable | PacketFlagFragment))) != 0U ||
        packet.header.reserved != 0U || packet.header.payload_bytes != reader.remaining()) {
        return std::nullopt;
    }
    packet.header.type = static_cast<PacketType>(type);
    std::span<const std::uint8_t> payload;
    if (!reader.read_bytes(packet.header.payload_bytes, payload) || !reader.empty()) {
        return std::nullopt;
    }
    packet.payload.assign(payload.begin(), payload.end());
    return packet;
}

std::optional<std::vector<std::uint8_t>> encode_hello(const HelloMessage& message) {
    ByteWriter writer;
    if (!writer.write_u64(message.client_nonce) || !writer.write_u32(message.build_id) ||
        !writer.write_string(message.player_name, maximum_player_name_bytes)) {
        return std::nullopt;
    }
    return writer.take();
}

std::optional<HelloMessage> decode_hello(std::span<const std::uint8_t> payload) {
    HelloMessage message{};
    ByteReader reader(payload);
    if (!reader.read_u64(message.client_nonce) || !reader.read_u32(message.build_id) ||
        !reader.read_string(message.player_name, maximum_player_name_bytes) || !reader.empty()) {
        return std::nullopt;
    }
    return message;
}

std::vector<std::uint8_t> encode_welcome(const WelcomeMessage& message) {
    ByteWriter writer;
    writer.write_u64(message.client_nonce);
    writer.write_u64(message.session_token);
    writer.write_u64(message.world_seed);
    writer.write_u32(message.server_tick);
    writer.write_i32(message.world_width);
    writer.write_i32(message.world_height);
    writer.write_u16(message.tick_rate);
    writer.write_u8(message.client_id);
    writer.write_u8(message.maximum_clients);
    return writer.take();
}

std::optional<WelcomeMessage> decode_welcome(std::span<const std::uint8_t> payload) {
    WelcomeMessage message{};
    ByteReader reader(payload);
    if (!reader.read_u64(message.client_nonce) || !reader.read_u64(message.session_token) ||
        !reader.read_u64(message.world_seed) || !reader.read_u32(message.server_tick) ||
        !reader.read_i32(message.world_width) || !reader.read_i32(message.world_height) ||
        !reader.read_u16(message.tick_rate) || !reader.read_u8(message.client_id) ||
        !reader.read_u8(message.maximum_clients) || !reader.empty() || message.world_width <= 0 ||
        message.world_height <= 0 || message.world_width > maximum_network_world_dimension ||
        message.world_height > maximum_network_world_dimension ||
        static_cast<std::int64_t>(message.world_width) *
                static_cast<std::int64_t>(message.world_height) >
            maximum_network_world_cells ||
        message.tick_rate == 0U || message.client_id == 0U ||
        message.client_id > message.maximum_clients || message.maximum_clients > maximum_players) {
        return std::nullopt;
    }
    return message;
}

std::vector<std::uint8_t> encode_input(const InputMessage& message) {
    ByteWriter writer;
    writer.write_u64(message.session_token);
    writer.write_u32(message.input_sequence);
    writer.write_u32(message.target_tick);
    writer.write_u8(static_cast<std::uint8_t>(message.kind));
    writer.write_i32(message.focus.x);
    writer.write_i32(message.focus.y);
    writer.write_i32(message.target.x);
    writer.write_i32(message.target.y);
    writer.write_u8(static_cast<std::uint8_t>(message.material));
    writer.write_u8(message.radius);
    return writer.take();
}

std::optional<InputMessage> decode_input(std::span<const std::uint8_t> payload) {
    InputMessage message{};
    std::uint8_t kind = 0;
    std::uint8_t material = 0;
    ByteReader reader(payload);
    if (!reader.read_u64(message.session_token) || !reader.read_u32(message.input_sequence) ||
        !reader.read_u32(message.target_tick) || !reader.read_u8(kind) ||
        !reader.read_i32(message.focus.x) || !reader.read_i32(message.focus.y) ||
        !reader.read_i32(message.target.x) || !reader.read_i32(message.target.y) ||
        !reader.read_u8(material) || !reader.read_u8(message.radius) || !reader.empty() ||
        kind > static_cast<std::uint8_t>(InputKind::Paint) ||
        !is_valid(static_cast<MaterialId>(material))) {
        return std::nullopt;
    }
    message.kind = static_cast<InputKind>(kind);
    message.material = static_cast<MaterialId>(material);
    return message;
}

std::vector<std::uint8_t> encode_snapshot(const SnapshotMessage& message) {
    ByteWriter writer;
    writer.write_u32(message.server_tick);
    writer.write_u64(message.state_hash);
    writer.write_u32(message.organism_population);
    writer.write_u16(message.agent_count);
    writer.write_u16(message.active_chunks);
    return writer.take();
}

std::optional<SnapshotMessage> decode_snapshot(std::span<const std::uint8_t> payload) {
    SnapshotMessage message{};
    ByteReader reader(payload);
    if (!reader.read_u32(message.server_tick) || !reader.read_u64(message.state_hash) ||
        !reader.read_u32(message.organism_population) || !reader.read_u16(message.agent_count) ||
        !reader.read_u16(message.active_chunks) || !reader.empty()) {
        return std::nullopt;
    }
    return message;
}

} // namespace meat2d::net
