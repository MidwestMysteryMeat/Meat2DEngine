#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace meat2d::net {

inline constexpr std::uint32_t protocol_magic = 0x4D32444EU; // "M2DN"
inline constexpr std::uint16_t protocol_version = 2;
inline constexpr std::uint16_t default_port = 27182;
inline constexpr std::uint16_t default_lan_discovery_port = 27183;
inline constexpr std::uint16_t default_directory_port = 27184;
inline constexpr std::uint8_t maximum_players = 8;
inline constexpr std::size_t maximum_datagram_bytes = 1'200;
inline constexpr std::size_t encoded_header_bytes = 28;
inline constexpr std::size_t maximum_player_name_bytes = 24;
inline constexpr std::int32_t maximum_network_world_dimension = 16'384;
inline constexpr std::int64_t maximum_network_world_cells = 32'000'000;

enum PacketFlags : std::uint8_t {
    PacketFlagNone = 0,
    PacketFlagReliable = 1U << 0U,
    PacketFlagFragment = 1U << 1U
};

enum class PacketType : std::uint8_t {
    Hello,
    Welcome,
    Input,
    Snapshot,
    ChunkDelta,
    Acknowledgement,
    Disconnect,
    LanQuery,
    LanAnnouncement,
    DirectoryRegister,
    DirectoryListRequest,
    DirectoryListResponse,
    DirectoryJoinRequest,
    DirectoryPunch,
    HolePunch
};

enum class InputKind : std::uint8_t { SetFocus, Paint };

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
