#pragma once

#include "meat2d/scene/Scene.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace meat2d::scene {

inline constexpr std::size_t maximum_scene_snapshot_bytes = 8U * 1024U * 1024U;

struct SceneSnapshot {
    std::uint64_t state_hash{};
    std::vector<std::uint8_t> bytes;
};

// Captures the versioned scene document plus its deterministic content hash.
// The bound keeps editor/autosave/network callers from accepting unbounded
// allocations before a transport-specific fragmentation policy is applied.
[[nodiscard]] std::optional<SceneSnapshot> capture_snapshot(
    const Scene& scene, std::size_t maximum_bytes = maximum_scene_snapshot_bytes);

// Decoding validates both the document and its expected state hash.
[[nodiscard]] std::optional<Scene> decode_snapshot(const SceneSnapshot& snapshot,
                                                   std::size_t maximum_bytes =
                                                       maximum_scene_snapshot_bytes);

// Restores only after a complete validation pass; on failure, target is unchanged.
bool restore_snapshot(Scene& target, const SceneSnapshot& snapshot,
                      std::size_t maximum_bytes = maximum_scene_snapshot_bytes);

} // namespace meat2d::scene
