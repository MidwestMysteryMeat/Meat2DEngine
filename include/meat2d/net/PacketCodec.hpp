#pragma once

#include "meat2d/core/Types.hpp"
#include "meat2d/net/Protocol.hpp"
#include "meat2d/sim/Material.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace meat2d::net {

class ByteWriter {
  public:
    explicit ByteWriter(std::size_t maximum_bytes = maximum_datagram_bytes);

    bool write_u8(std::uint8_t value);
    bool write_u16(std::uint16_t value);
    bool write_u32(std::uint32_t value);
    bool write_u64(std::uint64_t value);
    bool write_i16(std::int16_t value);
    bool write_i32(std::int32_t value);
    bool write_bytes(std::span<const std::uint8_t> bytes);
    bool write_string(std::string_view text, std::size_t maximum_length);

    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept;
    [[nodiscard]] std::vector<std::uint8_t> take() noexcept;

  private:
    [[nodiscard]] bool reserve_for(std::size_t bytes) const noexcept;

    std::size_t maximum_bytes_{};
    std::vector<std::uint8_t> bytes_;
};

class ByteReader {
  public:
    explicit ByteReader(std::span<const std::uint8_t> bytes);

    bool read_u8(std::uint8_t& value);
    bool read_u16(std::uint16_t& value);
    bool read_u32(std::uint32_t& value);
    bool read_u64(std::uint64_t& value);
    bool read_i16(std::int16_t& value);
    bool read_i32(std::int32_t& value);
    bool read_bytes(std::size_t count, std::span<const std::uint8_t>& value);
    bool read_string(std::string& value, std::size_t maximum_length);

    [[nodiscard]] std::size_t remaining() const noexcept;
    [[nodiscard]] bool empty() const noexcept;

  private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_{};
};

struct Packet {
    PacketHeader header{};
    std::vector<std::uint8_t> payload;
};

struct HelloMessage {
    std::uint64_t client_nonce{};
    std::uint32_t build_id{};
    std::string player_name;
};

struct WelcomeMessage {
    std::uint64_t client_nonce{};
    std::uint64_t session_token{};
    std::uint64_t world_seed{};
    std::uint32_t server_tick{};
    std::int32_t world_width{};
    std::int32_t world_height{};
    std::uint16_t tick_rate{60};
    std::uint8_t client_id{};
    std::uint8_t maximum_clients{maximum_players};
};

struct InputMessage {
    std::uint64_t session_token{};
    std::uint32_t input_sequence{};
    std::uint32_t target_tick{};
    InputKind kind{InputKind::SetFocus};
    Vec2i focus{};
    Vec2i target{};
    MaterialId material{MaterialId::Empty};
    std::uint8_t radius{};
};

struct SnapshotMessage {
    std::uint32_t server_tick{};
    std::uint64_t state_hash{};
    std::uint32_t organism_population{};
    std::uint16_t agent_count{};
    std::uint16_t active_chunks{};
};

[[nodiscard]] std::optional<std::vector<std::uint8_t>> encode_packet(
    PacketHeader header,
    std::span<const std::uint8_t> payload);
[[nodiscard]] std::optional<Packet> decode_packet(
    std::span<const std::uint8_t> datagram);

[[nodiscard]] std::optional<std::vector<std::uint8_t>> encode_hello(
    const HelloMessage& message);
[[nodiscard]] std::optional<HelloMessage> decode_hello(
    std::span<const std::uint8_t> payload);
[[nodiscard]] std::vector<std::uint8_t> encode_welcome(const WelcomeMessage& message);
[[nodiscard]] std::optional<WelcomeMessage> decode_welcome(
    std::span<const std::uint8_t> payload);
[[nodiscard]] std::vector<std::uint8_t> encode_input(const InputMessage& message);
[[nodiscard]] std::optional<InputMessage> decode_input(
    std::span<const std::uint8_t> payload);
[[nodiscard]] std::vector<std::uint8_t> encode_snapshot(const SnapshotMessage& message);
[[nodiscard]] std::optional<SnapshotMessage> decode_snapshot(
    std::span<const std::uint8_t> payload);

} // namespace meat2d::net
