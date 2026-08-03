#pragma once

#include "meat2d/core/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace meat2d::ai {

inline constexpr std::size_t maximum_crowd_agents = 16'384U;

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
};

} // namespace meat2d::ai
