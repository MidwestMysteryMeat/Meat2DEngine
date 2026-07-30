#include "meat2d/sim/Scenario.hpp"

#include "meat2d/sim/World.hpp"

#include <algorithm>

namespace meat2d {

void seed_sand_lab(World& world) {
    const auto floor_height = std::max(4, world.height() / 18);
    for (std::int32_t y = world.height() - floor_height; y < world.height(); ++y) {
        for (std::int32_t x = 0; x < world.width(); ++x) {
            world.set_material({x, y}, MaterialId::Stone);
        }
    }

    const auto platform_y = world.height() * 2 / 3;
    for (std::int32_t x = world.width() / 10; x < world.width() * 4 / 10; ++x) {
        if (x % 13 != 0) {
            world.set_material({x, platform_y}, MaterialId::Stone);
        }
    }
    for (std::int32_t x = world.width() * 6 / 10; x < world.width() * 9 / 10; ++x) {
        if (x % 17 != 0) {
            world.set_material({x, platform_y + world.height() / 12}, MaterialId::Stone);
        }
    }

    world.paint_disc(
        {world.width() / 4, world.height() / 5},
        std::max(6, world.height() / 12),
        MaterialId::Sand);

    const RectI reservoir{
        world.width() * 6 / 10,
        world.height() / 5,
        world.width() / 5,
        world.height() / 7,
    };
    for (std::int32_t y = reservoir.y; y < reservoir.y + reservoir.height; ++y) {
        for (std::int32_t x = reservoir.x; x < reservoir.x + reservoir.width; ++x) {
            world.set_material({x, y}, MaterialId::Water);
        }
    }

    world.wake_all();
}

void seed_elements_lab(World& world) {
    seed_sand_lab(world);

    const auto floor_height = std::max(4, world.height() / 18);
    const auto floor_y = world.height() - floor_height;

    // A damp soil bed demonstrates seeds, plant growth, and mud.
    const auto garden_start = world.width() / 32;
    const auto garden_end = world.width() / 5;
    for (std::int32_t x = garden_start; x < garden_end; ++x) {
        world.set_material({x, floor_y - 1}, MaterialId::Soil);
    }
    for (std::int32_t x = garden_start + 4; x < garden_end - 4; x += 9) {
        world.set_material({x, floor_y - 2}, MaterialId::Seed);
        world.set_material({x + 1, floor_y - 2}, MaterialId::Water);
    }

    // Conductive metal line with a one-shot electrical pulse.
    const auto wire_y = floor_y - std::max(8, world.height() / 12);
    for (std::int32_t x = world.width() * 7 / 20; x < world.width() * 11 / 20; ++x) {
        world.set_material({x, wire_y}, MaterialId::Metal);
    }
    world.set_material({world.width() * 7 / 20 - 1, wire_y}, MaterialId::Electricity);

    // A small combustible structure and powder fuse.
    const auto structure_x = world.width() * 11 / 20;
    const auto structure_height = std::max(10, world.height() / 10);
    for (std::int32_t y = floor_y - structure_height; y < floor_y; ++y) {
        world.set_material({structure_x, y}, MaterialId::Wood);
        world.set_material({structure_x + structure_height, y}, MaterialId::Wood);
    }
    for (std::int32_t x = structure_x; x <= structure_x + structure_height; ++x) {
        world.set_material({x, floor_y - structure_height}, MaterialId::Wood);
        world.set_material({x, floor_y - 1}, MaterialId::Gunpowder);
    }
    world.paint_disc(
        {structure_x + structure_height / 2, floor_y - 4},
        std::max(2, structure_height / 5),
        MaterialId::Oil);

    // Cold material and a guarded lava sample invite phase-change experiments.
    const auto sample_y = std::max(8, world.height() / 8);
    world.paint_disc(
        {world.width() * 13 / 20, sample_y},
        std::max(3, world.height() / 32),
        MaterialId::Snow);
    world.paint_disc(
        {world.width() * 17 / 20, sample_y},
        std::max(3, world.height() / 36),
        MaterialId::Lava);
    world.paint_disc(
        {world.width() * 18 / 20, sample_y},
        std::max(3, world.height() / 36),
        MaterialId::Water);

    // Corrosion target.
    const auto acid_x = world.width() * 9 / 10;
    const auto acid_y = floor_y - std::max(15, world.height() / 8);
    for (std::int32_t y = acid_y; y < floor_y; ++y) {
        world.set_material({acid_x, y}, MaterialId::Concrete);
    }
    world.paint_disc(
        {acid_x - std::max(4, world.width() / 50), acid_y},
        std::max(2, world.height() / 45),
        MaterialId::Acid);

    world.wake_all();
}

} // namespace meat2d
