#pragma once

#include "meat2d/scene/Scene.hpp"
#include "meat2d/sim/World.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <optional>

namespace meat2d::persistence {

inline constexpr std::uint16_t session_snapshot_format_version = 1;
inline constexpr std::size_t maximum_session_snapshot_bytes = 256U * 1024U * 1024U;

struct RestoredSession {
    std::uint64_t session_id{};
    World world;
    scene::Scene scene;
};

// Composes independently versioned World and Scene documents. The envelope
// intentionally does not claim to persist AI agents, projectiles, scripts, or
// transport state until those subsystems provide their own bounded codecs.
[[nodiscard]] std::vector<std::uint8_t> encode_session(
    std::uint64_t session_id, const World& world, const scene::Scene& scene,
    std::size_t maximum_bytes = maximum_session_snapshot_bytes);

// Validates envelope lengths, component hashes, and both component formats
// before constructing the restored state.
[[nodiscard]] std::optional<RestoredSession> decode_session(
    std::span<const std::uint8_t> bytes,
    std::size_t maximum_bytes = maximum_session_snapshot_bytes);

} // namespace meat2d::persistence
