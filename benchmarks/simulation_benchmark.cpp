#include "meat2d/sim/Scenario.hpp"
#include "meat2d/sim/World.hpp"

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>

int main() {
    constexpr std::uint64_t ticks = 600;
    meat2d::World world({
        .width = 640,
        .height = 360,
        .seed = 0xBEEFBEEF,
        .sleep_after_ticks = 30,
    });
    meat2d::seed_sand_lab(world);

    std::uint64_t moves = 0;
    const auto start = std::chrono::steady_clock::now();
    for (std::uint64_t tick = 0; tick < ticks; ++tick) {
        moves += world.step().moved_cells;
    }
    const double seconds = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - start)
                               .count();
    const double evaluated_cells =
        static_cast<double>(world.width()) * world.height() * ticks;

    std::cout << std::fixed << std::setprecision(2)
              << "world=" << world.width() << 'x' << world.height()
              << " ticks=" << ticks << " seconds=" << seconds
              << " theoretical_Mcells_per_second=" << evaluated_cells / seconds / 1'000'000.0
              << " moves=" << moves << " hash=0x" << std::hex << world.state_hash()
              << std::dec << '\n';
    return 0;
}
