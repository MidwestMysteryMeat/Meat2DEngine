#pragma once

#include "meat2d/core/Types.hpp"

#include <cstddef>
#include <cstdint>

namespace meat2d::ai {

inline constexpr std::size_t maximum_crowd_agents = 16'384U;
inline constexpr std::int32_t maximum_crowd_separation_radius = 64;
inline constexpr std::int32_t crowd_spatial_cell_size = 8;

struct CrowdAgent {
    std::uint32_t id{};
    Vec2i position{};
    Vec2i target{};
    std::int32_t max_step{1};
    std::int32_t separation_radius{2};
    bool enabled{true};

    friend bool operator==(const CrowdAgent&, const CrowdAgent&) = default;
};

struct CrowdStepStats {
    std::uint32_t agents{};
    std::uint32_t moved{};
    std::uint32_t separated{};
};

} // namespace meat2d::ai
