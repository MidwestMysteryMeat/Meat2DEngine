#pragma once

#include "meat2d/ai/CrowdTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace meat2d::ai {

// Deterministic uniform-grid broad phase for crowd neighbor queries. It owns
// no gameplay state and can be reused by other local-agent systems.
class CrowdSpatialIndex {
  public:
    explicit CrowdSpatialIndex(std::size_t maximum_index_agents = maximum_crowd_agents);

    void rebuild(std::span<const CrowdAgent> agents);
    [[nodiscard]] std::span<const std::size_t> query(Vec2i position,
                                                     std::int32_t radius);

  private:
    struct Entry {
        Vec2i cell{};
        std::uint32_t id{};
        std::size_t agent_index{};
    };

    std::vector<Entry> entries_;
    std::vector<std::size_t> candidates_;
};

} // namespace meat2d::ai
