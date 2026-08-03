#pragma once

#include "meat2d/scene/Scene.hpp"

namespace meat2d::scene {

struct MotionResult {
    Vec2i applied{};
    bool hit_horizontal{};
    bool hit_vertical{};
    bool grounded{};
};

struct PhysicsStepStats {
    std::uint32_t bodies{};
    std::uint32_t collisions{};
    std::uint32_t grounded{};
};

// Deterministic axis-separated kinematic movement for ordinary actors. It is
// deliberately smaller than a rigid-body solver and does not alter World
// materials; a Box2D-style solver can be added behind the same scene boundary.
[[nodiscard]] MotionResult move_and_collide(Scene& scene, EntityId entity, Vec2i delta);

[[nodiscard]] PhysicsStepStats step_rigid_bodies(Scene& scene, Vec2i gravity = {0, 1});

} // namespace meat2d::scene
