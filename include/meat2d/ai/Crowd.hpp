#pragma once

#include "meat2d/ai/CrowdTypes.hpp"
#include "meat2d/ai/CrowdSpatialIndex.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace meat2d::ai {

// Stable-ID integer crowd steering. It is intentionally navigation-backend
// neutral: a game can add tile/path queries later, while this layer provides
// bounded target seeking and deterministic local separation.
class CrowdSimulation {
  public:
    explicit CrowdSimulation(std::size_t max_agents = maximum_crowd_agents);

    bool add_agent(CrowdAgent agent);
    bool remove_agent(std::uint32_t id);
    bool set_target(std::uint32_t id, Vec2i target);
    [[nodiscard]] CrowdAgent* find(std::uint32_t id) noexcept;
    [[nodiscard]] const CrowdAgent* find(std::uint32_t id) const noexcept;
    [[nodiscard]] std::span<const CrowdAgent> agents() const noexcept;

    CrowdStepStats step(RectI bounds);
    [[nodiscard]] std::uint64_t state_hash() const noexcept;

  private:
    std::size_t maximum_agents_{};
    std::vector<CrowdAgent> agents_;

    std::vector<CrowdAgent> step_snapshot_;
    CrowdSpatialIndex spatial_index_;
};

} // namespace meat2d::ai
