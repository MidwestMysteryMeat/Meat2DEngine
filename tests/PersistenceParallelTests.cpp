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

void test_replay_round_trip_and_divergence() {
    meat2d::WorldConfig config{
        .width = 48,
        .height = 48,
        .seed = 99,
        .sleep_after_ticks = 30,
    };
    meat2d::World world(config);
    meat2d::replay::ReplayLog log(config);

    // Everything the reference run does to the world besides pure cellular
    // simulation has to go through the log, including initial level dressing
    // — play() only ever reconstructs a bare World from `config`.
    for (std::int32_t y = 0; y < world.height(); ++y) {
        log.record_paint(world.current_tick(), {24, y}, 0, meat2d::MaterialId::Stone);
        world.paint_disc({24, y}, 0, meat2d::MaterialId::Stone);
    }

    constexpr meat2d::Tick total_ticks = 60;
    for (meat2d::Tick tick = 0; tick < total_ticks; ++tick) {
        if (tick == 10) {
            log.record_paint(world.current_tick(), {10, 4}, 3, meat2d::MaterialId::Sand);
            world.paint_disc({10, 4}, 3, meat2d::MaterialId::Sand);
        }
        world.step();
        if (world.current_tick() % 20 == 0) {
            log.record_checkpoint(world.current_tick(), world.state_hash());
        }
    }

    check(!log.paint_events().empty(), "no paint events were recorded");
    check(!log.checkpoints().empty(), "no checkpoints were recorded");

    const auto encoded = log.encode();
    meat2d::replay::ReplayLog decoded;
    check(decoded.decode(encoded), "encoded replay log failed to decode");
    check(decoded.paint_events().size() == log.paint_events().size(),
          "decoded replay lost paint events");
    check(decoded.checkpoints().size() == log.checkpoints().size(),
          "decoded replay lost checkpoints");

    const auto matched = meat2d::replay::play(decoded, total_ticks);
    check(matched.outcome == meat2d::replay::ReplayOutcome::Matched,
          "replay did not reproduce the recorded run");
    check(matched.ticks_played == total_ticks, "matched replay reported the wrong tick count");

    // Rebuild the log with one checkpoint hash deliberately wrong to prove
    // play() detects a divergence at that exact tick, not only "the final
    // state differs".
    const auto tampered_tick = decoded.checkpoints().front().tick;
    const auto original_hash = decoded.checkpoints().front().state_hash;
    meat2d::replay::ReplayLog tampered(decoded.config());
    for (const auto& event : decoded.paint_events()) {
        tampered.record_paint(event.tick, event.position, event.radius, event.material);
    }
    bool tampered_once = false;
    for (const auto& checkpoint : decoded.checkpoints()) {
        if (!tampered_once && checkpoint.tick == tampered_tick) {
            tampered.record_checkpoint(checkpoint.tick, checkpoint.state_hash ^ 0xFFFFFFFFULL);
            tampered_once = true;
        } else {
            tampered.record_checkpoint(checkpoint.tick, checkpoint.state_hash);
        }
    }

    const auto diverged = meat2d::replay::play(tampered, total_ticks);
    check(diverged.outcome == meat2d::replay::ReplayOutcome::Diverged,
          "tampered checkpoint was not detected as a divergence");
    check(diverged.divergent_tick == tampered_tick, "divergence was reported at the wrong tick");
    check(diverged.expected_hash == (original_hash ^ 0xFFFFFFFFULL),
          "divergence reported the wrong expected hash");
    check(diverged.actual_hash == original_hash, "divergence reported the wrong actual hash");
}

void test_replay_decode_sorts_out_of_order_paint_events() {
    meat2d::WorldConfig config{
        .width = 48,
        .height = 48,
        .seed = 7,
        .sleep_after_ticks = 30,
    };

    // Reference run: two paints at ticks 2 and 5, checkpoint at the end.
    meat2d::World world(config);
    meat2d::replay::ReplayLog log(config);
    constexpr meat2d::Tick total_ticks = 12;
    for (meat2d::Tick tick = 0; tick < total_ticks; ++tick) {
        if (world.current_tick() == 2) {
            world.paint_disc({12, 4}, 2, meat2d::MaterialId::Sand);
        }
        if (world.current_tick() == 5) {
            world.paint_disc({30, 4}, 2, meat2d::MaterialId::Water);
        }
        world.step();
    }

    // Record the same events out of order (tick 5 before tick 2). Before
    // decode() sorted by tick, the head-first consumer in play() would
    // never reach the tick-2 event, silently drop both effects, and still
    // return Matched.
    log.record_paint(5, {30, 4}, 2, meat2d::MaterialId::Water);
    log.record_paint(2, {12, 4}, 2, meat2d::MaterialId::Sand);
    log.record_checkpoint(world.current_tick(), world.state_hash());

    meat2d::replay::ReplayLog decoded;
    check(decoded.decode(log.encode()), "out-of-order replay log failed to decode");
    const auto events = decoded.paint_events();
    check(events.size() == 2, "decode dropped paint events");
    check(events[0].tick == 2 && events[1].tick == 5,
          "decode did not sort paint events by tick");

    const auto result = meat2d::replay::play(decoded, total_ticks);
    check(result.outcome == meat2d::replay::ReplayOutcome::Matched,
          "sorted replay did not reproduce the reference run");
}

void test_chunk_store_persistence_across_worlds() {
    const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto directory = std::filesystem::temp_directory_path() / ("meat2d-chunkstore-" + unique);
    std::error_code error;
    std::filesystem::remove_all(directory, error);

    meat2d::World source({
        .width = 128,
        .height = 128,
        .seed = 55,
        .sleep_after_ticks = 30,
    });
    for (int y = 0; y < source.height(); ++y) {
        source.set_material({64, y}, meat2d::MaterialId::Stone);
    }
    source.paint_disc({20, 20}, 8, meat2d::MaterialId::Water);
    for (int tick = 0; tick < 30; ++tick) {
        source.step();
    }

    meat2d::ChunkStore store(directory);
    check(!store.has_chunk(0, 0), "chunk file existed before saving");
    const auto saved = store.save_all(source);
    check(saved == static_cast<std::size_t>(source.chunk_columns() * source.chunk_rows()),
          "save_all did not save every chunk");
    check(store.has_chunk(0, 0), "save_all did not create a file for chunk (0,0)");
    check(!std::filesystem::exists(directory / "chunk_0_0.m2dchunk.tmp") &&
              !std::filesystem::exists(directory / "chunk_0_0.m2dchunk.bak"),
          "chunk save left an atomic-write staging file behind");

    const auto chunk_file = directory / "chunk_0_0.m2dchunk";
    const auto backup_file = directory / "chunk_0_0.m2dchunk.bak";
    std::filesystem::rename(chunk_file, backup_file, error);
    check(!error, "chunk persistence test could not create a recovery backup");
    check(store.load_chunk(source, 0, 0) && std::filesystem::exists(chunk_file) &&
              !std::filesystem::exists(backup_file),
          "chunk loader did not recover an interrupted atomic replacement");

    // A freshly constructed World, not the same instance that was stepped
    // and saved — this is the actual persistence scenario: reloading a
    // world in a later process, not just round-tripping in place.
    meat2d::World restored({
        .width = 128,
        .height = 128,
        .seed = 55,
        .sleep_after_ticks = 30,
    });
    const auto loaded = store.load_all(restored);
    check(loaded == saved, "load_all did not load every saved chunk");

    // The disk path must normalize updated_epoch on load exactly like the
    // netcode decode path does; a stale saved epoch equal to the current
    // tick would make update_cell silently skip the restored cell.
    bool epochs_normalized = true;
    for (const auto& chunk : restored.chunks()) {
        for (const auto& cell : chunk.cells) {
            if (cell.updated_epoch != 0) {
                epochs_normalized = false;
            }
        }
    }
    check(epochs_normalized, "load_chunk_cells did not normalize updated_epoch");

    // state_hash() folds in current_tick(), which legitimately differs here
    // (source has stepped 30 times, restored has stepped 0) — chunk_hash()
    // is the tick-independent per-chunk comparison this scenario calls for.
    for (std::size_t index = 0; index < source.chunks().size(); ++index) {
        check(restored.chunk_hash(index) == source.chunk_hash(index),
              "restored chunk did not match the saved chunk's hash");
    }
    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x) {
            check(restored.material({x, y}) == source.material({x, y}),
                  "restored world has a mismatched cell");
        }
    }

    check(!store.load_chunk(restored, -1, 0), "load_chunk accepted an out-of-range column");
    check(!store.load_chunk(restored, source.chunk_columns(), 0),
          "load_chunk accepted a column past the grid");
    check(!store.has_chunk(-1, -1), "has_chunk reported a file for an out-of-range chunk");

    std::filesystem::remove_all(directory, error);
}

void test_parallel_step_deterministic_across_thread_counts() {
    meat2d::World single_threaded({
        .width = 192,
        .height = 128,
        .seed = 91,
        .sleep_after_ticks = 30,
    });
    meat2d::World multi_threaded({
        .width = 192,
        .height = 128,
        .seed = 91,
        .sleep_after_ticks = 30,
    });
    meat2d::seed_sand_lab(single_threaded);
    meat2d::seed_sand_lab(multi_threaded);

    for (int tick = 0; tick < 180; ++tick) {
        single_threaded.step_parallel(1);
        multi_threaded.step_parallel(5);
        check(single_threaded.state_hash() == multi_threaded.state_hash(),
              "step_parallel produced a different result with a different worker count");
        if (failures != 0) {
            return;
        }
    }
}

void test_parallel_step_reproducible_across_runs() {
    meat2d::World first({
        .width = 192,
        .height = 128,
        .seed = 92,
        .sleep_after_ticks = 30,
    });
    meat2d::World second({
        .width = 192,
        .height = 128,
        .seed = 92,
        .sleep_after_ticks = 30,
    });
    meat2d::seed_sand_lab(first);
    meat2d::seed_sand_lab(second);

    for (int tick = 0; tick < 180; ++tick) {
        first.step_parallel(4);
        second.step_parallel(4);
        check(first.state_hash() == second.state_hash(),
              "two identically seeded worlds diverged under step_parallel");
        if (failures != 0) {
            return;
        }
    }
}

void test_parallel_step_conserves_water_and_settles_sand() {
    meat2d::World world({
        .width = 128,
        .height = 96,
        .seed = 93,
        .sleep_after_ticks = 30,
    });
    for (int x = 0; x < world.width(); ++x) {
        world.set_material({x, world.height() - 1}, meat2d::MaterialId::Stone);
    }
    const auto painted_water = world.paint_disc({64, 8}, 6, meat2d::MaterialId::Water);
    world.paint_disc({20, 4}, 4, meat2d::MaterialId::Sand);

    for (int tick = 0; tick < 200; ++tick) {
        world.step_parallel(4);
    }

    std::size_t water_cells = 0;
    bool sand_on_floor = false;
    for (int y = 0; y < world.height(); ++y) {
        for (int x = 0; x < world.width(); ++x) {
            const auto material = world.material({x, y});
            if (material == meat2d::MaterialId::Water) {
                ++water_cells;
            }
            if (material == meat2d::MaterialId::Sand && y == world.height() - 2) {
                sand_on_floor = true;
            }
        }
    }
    check(water_cells == painted_water, "step_parallel changed the water cell count");
    check(sand_on_floor, "step_parallel did not settle sand onto the floor");
}

void test_parallel_step_records_dirty_regions() {
    meat2d::World world({
        .width = 96,
        .height = 96,
        .seed = 94,
        .sleep_after_ticks = 30,
    });
    world.paint_disc({40, 8}, 6, meat2d::MaterialId::Sand);
    world.paint_disc({70, 8}, 5, meat2d::MaterialId::Water);

    const auto pixel_count = 96U * 96U * 4U;
    std::vector<std::uint8_t> partial(pixel_count);
    world.rasterize_rgba(partial);
    world.clear_dirty();

    for (int tick = 0; tick < 24; ++tick) {
        world.step_parallel(3);
    }

    // Same property test_dirty_region_rasterization proves for step(): patch
    // `partial` (a pre-step snapshot) using only the regions step_parallel's
    // deferred mark_changed reported dirty, and it must exactly reproduce a
    // fresh full rasterization — proving the thread_local touch-log merge
    // didn't drop any changed cell.
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
                  "step_parallel's dirty rect escaped the world bounds");
            world.rasterize_rgba_region(region, partial);
        }
    }
    check(found_dirty, "step_parallel's deferred mark_changed produced no dirty regions");

    std::vector<std::uint8_t> full(pixel_count);
    world.rasterize_rgba(full);
    check(full == partial,
          "step_parallel's dirty-region refresh did not reproduce the full rasterization — "
          "the deferred touch-log merge dropped a changed cell");
}

} // namespace meat2d_tests
