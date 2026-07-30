#pragma once

#include <cstdint>
#include <type_traits>

namespace meat2d::net {

inline constexpr std::uint32_t protocol_magic = 0x4D32444EU; // "M2DN"
inline constexpr std::uint16_t protocol_version = 1;
inline constexpr std::uint16_t default_port = 27182;
inline constexpr std::uint8_t maximum_players = 8;

enum class PacketType : std::uint8_t {
    Hello,
    Welcome,
    Input,
    Snapshot,
    ChunkDelta,
    Acknowledgement,
    Disconnect
};

struct PacketHeader {
    std::uint32_t magic{protocol_magic};
    std::uint16_t version{protocol_version};
    PacketType type{PacketType::Hello};
    std::uint8_t flags{};
    std::uint32_t sequence{};
    std::uint32_t acknowledgement{};
    std::uint32_t acknowledgement_bits{};
    std::uint32_t server_tick{};
    std::uint16_t payload_bytes{};
    std::uint16_t reserved{};
};

static_assert(sizeof(PacketHeader) == 28);
static_assert(std::is_trivially_copyable_v<PacketHeader>);

} // namespace meat2d::net
