#pragma once

#include "meat2d/core/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace meat2d {
class World;
}

namespace meat2d::net {

inline constexpr std::uint8_t chunk_codec_version = 2;
inline constexpr std::size_t maximum_chunk_delta_bytes = 40'000;

struct ChunkDeltaInfo {
    std::uint16_t chunk_x{};
    std::uint16_t chunk_y{};
    std::uint64_t revision{};
    std::uint64_t chunk_hash{};
    std::uint32_t changed_cells{};
};

[[nodiscard]] std::optional<std::vector<std::uint8_t>> encode_chunk_delta(
    const World& world,
    std::size_t chunk_index);
[[nodiscard]] std::optional<ChunkDeltaInfo> apply_chunk_delta(
    World& world,
    std::span<const std::uint8_t> payload);
[[nodiscard]] std::vector<std::size_t> interested_chunks(
    const World& world,
    Vec2i focus,
    std::uint8_t radius_in_chunks);

} // namespace meat2d::net
