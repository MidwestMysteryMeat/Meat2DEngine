#pragma once

#include "meat2d/core/Types.hpp"
#include "meat2d/sim/Material.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace meat2d {

class World;

using ProjectileId = std::uint32_t;

struct ProjectileConfig {
    // Integer cells traveled per tick. Both magnitude (speed) and direction
    // come from this vector; there is no normalization step, so trajectories
    // stay exact integer math like the rest of the simulation.
    Vec2i velocity{1, 0};
    std::int32_t max_ticks{120};
    // 0 affects only the cell the projectile hits; >0 paints a paint_disc
    // crater of that radius centered on the hit.
    std::int32_t impact_radius{0};
    MaterialId impact_material{MaterialId::Empty};
};

struct Projectile {
    ProjectileId id{};
    Vec2i position{};
    ProjectileConfig config{};
    std::int32_t ticks_alive{};
    bool alive{true};
};

// Deterministic, terrain-destroying projectiles built on World::raycast.
// Every tick a projectile advances by its integer velocity; if the segment
// it just traveled is blocked (see blocks_line_of_sight), it detonates on
// the blocking cell and its alive flag drops. A detonated projectile stays
// in projectiles() for exactly the tick it died on — enough for a caller to
// react with splash damage or effects at its final position — and is pruned
// at the start of the following step(). Pure integer math and live cell
// reads, so replays and multiplayer clients reproduce identical trajectories
// and impacts, and destroyed terrain is reflected immediately.
class ProjectileSystem {
  public:
    ProjectileId spawn(Vec2i origin, ProjectileConfig config);
    void step(World& world);
    void clear() noexcept;

    [[nodiscard]] std::span<const Projectile> projectiles() const noexcept;

  private:
    std::vector<Projectile> projectiles_;
    ProjectileId next_id_{1};
};

} // namespace meat2d
