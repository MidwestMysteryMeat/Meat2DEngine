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

} // namespace meat2d
