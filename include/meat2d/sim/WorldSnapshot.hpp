#pragma once

#include "meat2d/sim/World.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace meat2d::persistence {

inline constexpr std::uint16_t world_snapshot_format_version = 1;
inline constexpr std::size_t maximum_world_snapshot_bytes = 256U * 1024U * 1024U;

// Encodes the authoritative cellular World only. Updated-cell epochs and
// rendering dirty metadata are derived/transient and are normalized on load.
// Scenes, entities, agents, projectiles, and session metadata require their
// own versioned codecs and are intentionally not hidden in this format.
[[nodiscard]] std::vector<std::uint8_t> encode_world(
    const World& world, std::size_t maximum_bytes = maximum_world_snapshot_bytes);

// Returns an independently constructed World on success. The input is
// bounded before allocation and unknown/truncated versions are rejected.
[[nodiscard]] std::optional<World> decode_world(
    std::span<const std::uint8_t> bytes,
    std::size_t maximum_bytes = maximum_world_snapshot_bytes);

} // namespace meat2d::persistence
