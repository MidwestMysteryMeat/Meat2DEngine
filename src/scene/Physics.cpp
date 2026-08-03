#include "meat2d/scene/Physics.hpp"

#include <algorithm>
#include <cstdint>

namespace meat2d::scene {
namespace {

bool collides_with_solid(const Scene& scene, EntityId moving_entity) {
    const auto* entity = scene.find(moving_entity);
    if (entity == nullptr || !entity->collider || !entity->transform) {
        return false;
    }
    const auto bounds = scene.world_collider_bounds(moving_entity);
    if (!bounds) {
        return false;
    }
    const auto hits = scene.query_colliders(*bounds, false);
    const auto& moving_collider = *entity->collider;
    return std::any_of(hits.begin(), hits.end(), [&](EntityId hit) {
        if (hit == moving_entity) {
            return false;
        }
        const auto* other = scene.find(hit);
        return other != nullptr && other->collider &&
               (moving_collider.mask_bits & other->collider->category_bits) != 0U &&
               (other->collider->mask_bits & moving_collider.category_bits) != 0U;
    });
}

} // namespace

MotionResult move_and_collide(Scene& scene, EntityId entity, Vec2i delta) {
    MotionResult result{};
    auto* value = scene.find(entity);
    if (value == nullptr || !value->transform || !value->collider || !value->enabled) {
        return result;
    }

    auto& position = value->transform->position;
    const auto move_axis = [&](std::int32_t amount, bool horizontal) {
        if (amount == 0) {
            return;
        }
        const auto direction = amount > 0 ? 1 : -1;
        const auto steps = amount > 0
                               ? static_cast<std::uint32_t>(amount)
                               : static_cast<std::uint32_t>(-static_cast<std::int64_t>(amount));
        for (std::uint32_t step = 0; step < steps; ++step) {
            if (horizontal) {
                ++position.x;
                if (direction < 0) {
                    position.x -= 2;
                }
            } else {
                ++position.y;
                if (direction < 0) {
                    position.y -= 2;
                }
            }
            if (collides_with_solid(scene, entity)) {
                if (horizontal) {
                    position.x -= direction;
                    result.hit_horizontal = true;
                } else {
                    position.y -= direction;
                    result.hit_vertical = true;
                    result.grounded = direction > 0;
                }
                break;
            }
            if (horizontal) {
                result.applied.x += direction;
            } else {
                result.applied.y += direction;
            }
        }
    };
    move_axis(delta.x, true);
    move_axis(delta.y, false);
    return result;
}

PhysicsStepStats step_rigid_bodies(Scene& scene, Vec2i gravity) {
    PhysicsStepStats stats{};
    for (const auto& snapshot : scene.entities()) {
        if (!snapshot.rigid_body || !snapshot.rigid_body->dynamic) {
            continue;
        }
        ++stats.bodies;
        const auto entity = snapshot.id;
        auto* current = scene.find(entity);
        if (current == nullptr || !current->rigid_body) {
            continue;
        }
        auto& body = *current->rigid_body;
        body.velocity.x += body.acceleration.x;
        body.velocity.y += body.acceleration.y;
        if (body.affected_by_gravity) {
            body.velocity.x += gravity.x;
            body.velocity.y += gravity.y;
        }
        const auto max_velocity_x = std::max(0, body.max_velocity.x);
        const auto max_velocity_y = std::max(0, body.max_velocity.y);
        body.velocity.x = std::clamp(body.velocity.x, -max_velocity_x, max_velocity_x);
        body.velocity.y = std::clamp(body.velocity.y, -max_velocity_y, max_velocity_y);
        const auto motion = move_and_collide(scene, entity, body.velocity);
        if (motion.hit_horizontal || motion.hit_vertical) {
            ++stats.collisions;
        }
        if (motion.grounded) {
            ++stats.grounded;
        }
        if (motion.hit_horizontal) {
            body.velocity.x = 0;
        }
        if (motion.hit_vertical) {
            body.velocity.y = 0;
        }
    }
    return stats;
}

} // namespace meat2d::scene
