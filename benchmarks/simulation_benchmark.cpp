#include "meat2d/sim/Scenario.hpp"
#include "meat2d/sim/World.hpp"

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <thread>

namespace {

struct Result {
    double seconds{};
    std::uint64_t moves{};
    std::uint64_t hash{};
};

template <typename StepFn>
Result run(std::uint64_t ticks, StepFn&& step_fn) {
    meat2d::World world({
        .width = 640,
        .height = 360,
        .seed = 0xBEEFBEEF,
        .sleep_after_ticks = 30,
    });
    meat2d::seed_sand_lab(world);

    Result result{};
    const auto start = std::chrono::steady_clock::now();
    for (std::uint64_t tick = 0; tick < ticks; ++tick) {
        result.moves += step_fn(world).moved_cells;
    }
    result.seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    result.hash = world.state_hash();
    return result;
}

void report(const char* label, const Result& result, std::uint64_t ticks) {
    const double evaluated_cells = 640.0 * 360.0 * static_cast<double>(ticks);
    std::cout << std::fixed << std::setprecision(2) << label << " world=640x360 ticks=" << ticks
              << " seconds=" << result.seconds << " theoretical_Mcells_per_second="
              << evaluated_cells / result.seconds / 1'000'000.0 << " moves=" << result.moves
              << " hash=0x" << std::hex << result.hash << std::dec << '\n';
}

} // namespace

int main() {
    constexpr std::uint64_t ticks = 600;

    const auto serial = run(ticks, [](meat2d::World& world) { return world.step(); });
    report("step()        ", serial, ticks);

    const auto hardware_threads = std::max<unsigned>(1U, std::thread::hardware_concurrency());
    const auto parallel = run(ticks, [](meat2d::World& world) { return world.step_parallel(0); });
    std::cout << "(step_parallel used " << hardware_threads << " worker threads)\n";
    report("step_parallel()", parallel, ticks);

    return 0;
}
