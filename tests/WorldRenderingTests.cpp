#include "meat2d/ai/LivingSimulation.hpp"
#include "meat2d/ai/Crowd.hpp"
#include "meat2d/ai/LearningAgent.hpp"
#include "meat2d/ai/LearningEnvironment.hpp"
#include "meat2d/ai/NeuralNetwork.hpp"
#include "meat2d/assets/Animation.hpp"
#include "meat2d/assets/SpriteSheet.hpp"
#include "meat2d/assets/TileMap.hpp"
#include "meat2d/assets/TextureAtlas.hpp"
#include "meat2d/core/DeterministicRng.hpp"
#include "meat2d/core/FixedTimestep.hpp"
#include "meat2d/input/Input.hpp"
#include "meat2d/net/ChunkCodec.hpp"
#include "meat2d/net/Discovery.hpp"
#include "meat2d/net/Fragmentation.hpp"
#include "meat2d/net/PacketCodec.hpp"
#include "meat2d/net/Protocol.hpp"
#include "meat2d/net/Reliability.hpp"
#include "meat2d/net/Session.hpp"
#include "meat2d/net/UdpSocket.hpp"
#include "meat2d/replay/Replay.hpp"
#include "meat2d/render/Camera.hpp"
#include "meat2d/render/DebugDraw.hpp"
#include "meat2d/render/Particles.hpp"
#include "meat2d/render/SpriteBatch.hpp"
#include "meat2d/render/StaticMeshBatch.hpp"
#include "meat2d/render/WorldView.hpp"
#include "meat2d/scene/Physics.hpp"
#include "meat2d/scene/Scene.hpp"
#include "meat2d/scene/SceneHistory.hpp"
#include "meat2d/scene/SceneSnapshot.hpp"
#include "meat2d/scene/SceneStack.hpp"
#include "meat2d/sim/ChunkStore.hpp"
#include "meat2d/sim/Projectile.hpp"
#include "meat2d/sim/Scenario.hpp"
#include "meat2d/sim/World.hpp"
#include "meat2d/tools/ProjectBrowser.hpp"
#include "meat2d/tools/ProjectManager.hpp"
#include "meat2d/tools/SceneEditor.hpp"
#include "meat2d/tools/McpGateway.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include "TestSupport.hpp"

namespace meat2d_tests {

void test_cross_chunk_motion() {
    meat2d::World world({
        .width = 128,
        .height = 128,
        .seed = 33,
        .sleep_after_ticks = 30,
    });
    world.set_material({70, 62}, meat2d::MaterialId::Sand);
    for (int tick = 0; tick < 4; ++tick) {
        world.step();
    }
    check(world.material({70, 66}) == meat2d::MaterialId::Sand,
          "sand failed to cross a chunk boundary");
}

void test_determinism() {
    meat2d::World first({
        .width = 192,
        .height = 128,
        .seed = 44,
        .sleep_after_ticks = 30,
    });
    meat2d::World second({
        .width = 192,
        .height = 128,
        .seed = 44,
        .sleep_after_ticks = 30,
    });
    meat2d::seed_sand_lab(first);
    meat2d::seed_sand_lab(second);

    for (int tick = 0; tick < 240; ++tick) {
        first.step();
        second.step();
        check(first.state_hash() == second.state_hash(), "equal worlds diverged");
        if (failures != 0) {
            return;
        }
    }
}

void test_chunks_sleep() {
    meat2d::World world({
        .width = 128,
        .height = 128,
        .seed = 55,
        .sleep_after_ticks = 5,
    });
    world.wake_all();
    meat2d::TickStats stats{};
    for (int tick = 0; tick < 6; ++tick) {
        stats = world.step();
    }
    check(stats.active_chunks == 0, "quiet chunks did not enter sleep");
    check(stats.sleeping_chunks == 4, "unexpected sleeping chunk count");
}

void test_dirty_region_rasterization() {
    meat2d::World world({
        .width = 96,
        .height = 96,
        .seed = 77,
        .sleep_after_ticks = 30,
    });
    world.paint_disc({40, 8}, 6, meat2d::MaterialId::Sand);
    world.paint_disc({70, 8}, 5, meat2d::MaterialId::Water);

    const auto pixel_count = 96U * 96U * 4U;
    std::vector<std::uint8_t> full(pixel_count);
    std::vector<std::uint8_t> partial(pixel_count);
    world.rasterize_rgba(full);
    world.rasterize_rgba_region({0, 0, 96, 96}, partial);
    check(full == partial, "full-world region rasterization diverged from rasterize_rgba");

    world.rasterize_rgba(partial);
    world.clear_dirty();
    bool any_dirty = false;
    for (std::int32_t row = 0; row < world.chunk_rows(); ++row) {
        for (std::int32_t column = 0; column < world.chunk_columns(); ++column) {
            any_dirty = any_dirty || !world.chunk_dirty_rect(column, row).empty();
        }
    }
    check(!any_dirty, "clear_dirty left a non-empty chunk dirty rect");

    for (int tick = 0; tick < 24; ++tick) {
        world.step();
    }
    bool found_dirty = false;
    for (std::int32_t row = 0; row < world.chunk_rows(); ++row) {
        for (std::int32_t column = 0; column < world.chunk_columns(); ++column) {
            const auto region = world.chunk_dirty_rect(column, row);
            if (region.empty()) {
                continue;
            }
            found_dirty = true;
            check(region.x >= 0 && region.y >= 0 && region.x + region.width <= 96 &&
                      region.y + region.height <= 96,
                  "chunk dirty rect escaped the world bounds");
            world.rasterize_rgba_region(region, partial);
        }
    }
    check(found_dirty, "falling material produced no dirty regions");
    world.rasterize_rgba(full);
    check(full == partial, "dirty-region refresh did not reproduce the full rasterization");
}

void test_raster_output() {
    meat2d::World world({
        .width = 8,
        .height = 8,
        .seed = 66,
        .sleep_after_ticks = 30,
    });
    world.set_material({3, 4}, meat2d::MaterialId::Sand);
    std::vector<std::uint8_t> pixels(8U * 8U * 4U);
    world.rasterize_rgba(pixels);
    const auto offset = (4U * 8U + 3U) * 4U;
    check(pixels[offset] > pixels[offset + 2U], "sand pixel did not use a warm color");
    check(pixels[offset + 3U] == 255, "raster alpha is not opaque");
}

void test_raycast_and_line_of_sight() {
    meat2d::World world({
        .width = 32,
        .height = 32,
        .seed = 77,
        .sleep_after_ticks = 30,
    });

    const auto clear = world.raycast({1, 5}, {30, 5});
    check(!clear.blocked, "raycast over open space reported a block");
    check(clear.position == meat2d::Vec2i{30, 5}, "unblocked raycast did not report the target");
    check(world.line_of_sight({1, 5}, {30, 5}), "line_of_sight disagreed with an unblocked raycast");

    for (int y = 0; y < world.height(); ++y) {
        world.set_material({15, y}, meat2d::MaterialId::Stone);
    }
    const auto blocked = world.raycast({1, 5}, {30, 5});
    check(blocked.blocked, "raycast passed through a solid wall");
    check(blocked.position == meat2d::Vec2i{15, 5}, "raycast did not stop at the wall face");
    check(blocked.material == meat2d::MaterialId::Stone, "raycast reported the wrong blocking material");
    check(!world.line_of_sight({1, 5}, {30, 5}), "line_of_sight disagreed with a blocked raycast");

    world.set_material({15, 5}, meat2d::MaterialId::Empty);
    const auto reopened = world.raycast({1, 5}, {30, 5});
    check(!reopened.blocked, "destroying the wall did not reopen the raycast");
    check(world.line_of_sight({1, 5}, {30, 5}),
          "line_of_sight did not recover after the wall was destroyed");

    const auto aimed_at_wall = world.raycast({1, 6}, {15, 6});
    check(!aimed_at_wall.blocked, "aiming directly at a wall should not itself count as blocked");
    check(aimed_at_wall.position == meat2d::Vec2i{15, 6}, "raycast at a wall must report the wall cell");
    check(aimed_at_wall.material == meat2d::MaterialId::Stone,
          "raycast did not report the aimed-at wall's material");

    const auto out_of_bounds = world.raycast({-5, 5}, {10, 5});
    check(out_of_bounds.blocked, "out-of-bounds origin must report blocked, not a clear line");
    check(out_of_bounds.position == meat2d::Vec2i{-5, 5},
          "out-of-bounds origin raycast should report the offending endpoint");

    const auto oob_target = world.raycast({10, 5}, {200, 5});
    check(oob_target.blocked, "out-of-bounds target must report blocked, not a clear line");
    check(!world.line_of_sight({-5, 5}, {10, 5}),
          "line_of_sight through an out-of-bounds endpoint must be false");
}

void test_projectile_system_destroys_terrain() {
    meat2d::World world({
        .width = 64,
        .height = 64,
        .seed = 88,
        .sleep_after_ticks = 30,
    });
    for (int y = 0; y < world.height(); ++y) {
        world.set_material({40, y}, meat2d::MaterialId::Stone);
    }

    meat2d::ProjectileSystem projectiles;
    projectiles.spawn(
        {2, 32},
        {
            .velocity = {2, 0},
            .max_ticks = 100,
            .impact_radius = 0,
            .impact_material = meat2d::MaterialId::Empty,
        });

    bool detonated = false;
    for (int tick = 0; tick < 40 && !detonated; ++tick) {
        projectiles.step(world);
        for (const auto& projectile : projectiles.projectiles()) {
            if (!projectile.alive) {
                check(projectile.position == meat2d::Vec2i{40, 32},
                      "projectile did not detonate on the wall it hit");
                detonated = true;
            }
        }
    }
    check(detonated, "projectile never detonated against the wall");
    check(world.material({40, 32}) == meat2d::MaterialId::Empty,
          "projectile impact did not carve the wall");

    projectiles.step(world);
    check(projectiles.projectiles().empty(), "detonated projectile was not pruned next step");
}

void test_projectile_expires_without_impact() {
    meat2d::World world({
        .width = 16,
        .height = 16,
        .seed = 89,
        .sleep_after_ticks = 30,
    });
    meat2d::ProjectileSystem projectiles;
    projectiles.spawn(
        {1, 8},
        {
            .velocity = {1, 0},
            .max_ticks = 3,
            .impact_radius = 0,
            .impact_material = meat2d::MaterialId::Fire,
        });
    for (int tick = 0; tick < 3; ++tick) {
        projectiles.step(world);
    }
    check(projectiles.projectiles().size() == 1,
          "projectile should still be visible on its expiry tick");
    check(!projectiles.projectiles().front().alive, "projectile did not expire at max_ticks");

    projectiles.step(world);
    check(projectiles.projectiles().empty(), "expired projectile was not pruned");
    for (int x = 0; x < world.width(); ++x) {
        for (int y = 0; y < world.height(); ++y) {
            check(world.material({x, y}) == meat2d::MaterialId::Empty,
                  "expiring in open space should not modify terrain");
        }
    }
}

void test_projectile_leaves_world_without_impact() {
    meat2d::World world({
        .width = 16,
        .height = 16,
        .seed = 90,
        .sleep_after_ticks = 30,
    });
    meat2d::ProjectileSystem projectiles;
    projectiles.spawn(
        {14, 8},
        {
            .velocity = {5, 0},
            .max_ticks = 50,
            .impact_radius = 0,
            .impact_material = meat2d::MaterialId::Fire,
        });

    projectiles.step(world);
    check(projectiles.projectiles().size() == 1 && !projectiles.projectiles().front().alive,
          "projectile leaving the world bounds should die without detonating");

    projectiles.step(world);
    check(projectiles.projectiles().empty(), "out-of-bounds projectile was not pruned");
}

} // namespace meat2d_tests

