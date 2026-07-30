#include "meat2d/net/Protocol.hpp"
#include "meat2d/sim/Scenario.hpp"
#include "meat2d/sim/World.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void test_cell_layout_and_protocol() {
    check(sizeof(meat2d::Cell) == 8, "authoritative cell must remain eight bytes");
    check(
        sizeof(meat2d::net::PacketHeader) == 28,
        "network header layout unexpectedly changed");
    check(
        meat2d::net::maximum_players == 8,
        "first multiplayer target must remain eight players");
}

void test_sand_falls_and_stone_stays() {
    meat2d::World world({
        .width = 64,
        .height = 64,
        .seed = 11,
        .sleep_after_ticks = 30,
    });
    for (int x = 0; x < world.width(); ++x) {
        world.set_material({x, 60}, meat2d::MaterialId::Stone);
    }
    world.set_material({32, 2}, meat2d::MaterialId::Sand);

    for (int tick = 0; tick < 80; ++tick) {
        world.step();
    }

    check(
        world.material({32, 59}) == meat2d::MaterialId::Sand,
        "sand did not settle immediately above stone");
    check(
        world.material({32, 60}) == meat2d::MaterialId::Stone,
        "stone moved during cellular simulation");
}

void test_water_conserves_cells() {
    meat2d::World world({
        .width = 96,
        .height = 64,
        .seed = 22,
        .sleep_after_ticks = 30,
    });
    for (int x = 0; x < world.width(); ++x) {
        world.set_material({x, 62}, meat2d::MaterialId::Stone);
    }
    const auto painted = world.paint_disc({48, 8}, 5, meat2d::MaterialId::Water);
    for (int tick = 0; tick < 100; ++tick) {
        world.step();
    }

    std::size_t water_cells = 0;
    int lowest_water = 0;
    for (int y = 0; y < world.height(); ++y) {
        for (int x = 0; x < world.width(); ++x) {
            if (world.material({x, y}) == meat2d::MaterialId::Water) {
                ++water_cells;
                lowest_water = std::max(lowest_water, y);
            }
        }
    }
    check(water_cells == painted, "water cell count changed while flowing");
    check(lowest_water == 61, "water did not reach the floor");
}

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
    check(
        world.material({70, 66}) == meat2d::MaterialId::Sand,
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

} // namespace

int main() {
    try {
        test_cell_layout_and_protocol();
        test_sand_falls_and_stone_stays();
        test_water_conserves_cells();
        test_cross_chunk_motion();
        test_determinism();
        test_chunks_sleep();
        test_raster_output();
    } catch (const std::exception& exception) {
        std::cerr << "UNCAUGHT: " << exception.what() << '\n';
        return 1;
    }

    if (failures == 0) {
        std::cout << "MEAT2D TESTS PASS\n";
        return 0;
    }
    std::cerr << failures << " test assertion(s) failed\n";
    return 1;
}
