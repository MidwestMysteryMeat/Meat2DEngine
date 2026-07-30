#include "meat2d/sim/Projectile.hpp"

#include "meat2d/sim/World.hpp"

#include <algorithm>

namespace meat2d {

ProjectileId ProjectileSystem::spawn(Vec2i origin, ProjectileConfig config) {
    const auto id = next_id_++;
    projectiles_.push_back(Projectile{id, origin, config, 0, true});
    return id;
}

void ProjectileSystem::step(World& world) {
    // Prune projectiles that detonated last tick, giving callers one full
    // step() cycle to observe an impact (position, alive == false) before it
    // disappears — enough time to react with splash damage or effects.
    projectiles_.erase(
        std::remove_if(
            projectiles_.begin(), projectiles_.end(),
            [](const Projectile& projectile) { return !projectile.alive; }),
        projectiles_.end());

    for (auto& projectile : projectiles_) {
        ++projectile.ticks_alive;

        const Vec2i target{
            projectile.position.x + projectile.config.velocity.x,
            projectile.position.y + projectile.config.velocity.y,
        };
        if (!world.in_bounds(target)) {
            projectile.alive = false;
            continue;
        }

        // World::raycast never reports its own target as a blocker (aiming a
        // ray directly at a wall should show the wall, not a self-block). A
        // projectile flying into that same cell is a different question —
        // its destination becoming solid is exactly a collision — so check
        // it separately from the intermediate-cell raycast result.
        const auto hit = world.raycast(projectile.position, target);
        const bool target_blocks = blocks_line_of_sight(world.material(target));
        if (hit.blocked || target_blocks) {
            const auto impact_position = hit.blocked ? hit.position : target;
            if (projectile.config.impact_radius > 0) {
                world.paint_disc(impact_position, projectile.config.impact_radius,
                                 projectile.config.impact_material);
            } else {
                world.set_material(impact_position, projectile.config.impact_material);
            }
            projectile.position = impact_position;
            projectile.alive = false;
            continue;
        }

        projectile.position = target;
        if (projectile.ticks_alive >= projectile.config.max_ticks) {
            projectile.alive = false;
        }
    }
}

void ProjectileSystem::clear() noexcept {
    projectiles_.clear();
}

std::span<const Projectile> ProjectileSystem::projectiles() const noexcept {
    return projectiles_;
}

} // namespace meat2d
