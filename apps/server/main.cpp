#include "meat2d/core/Version.hpp"
#include "meat2d/net/Protocol.hpp"
#include "meat2d/sim/Scenario.hpp"
#include "meat2d/sim/World.hpp"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string_view>

namespace {

std::uint64_t parse_ticks(int argc, char** argv) {
    std::uint64_t ticks = 600;
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::string_view(argv[index]) != "--ticks") {
            continue;
        }
        const std::string_view text(argv[index + 1]);
        std::uint64_t parsed = 0;
        const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
        if (result.ec == std::errc{} && result.ptr == text.data() + text.size()) {
            ticks = parsed;
        }
    }
    return ticks;
}

} // namespace

int main(int argc, char** argv) {
    const auto requested_ticks = parse_ticks(argc, argv);
    meat2d::World world({
        .width = 320,
        .height = 180,
        .seed = 0x4D4541543244ULL,
        .sleep_after_ticks = 30,
    });
    meat2d::seed_sand_lab(world);

    std::uint64_t total_moves = 0;
    const auto start = std::chrono::steady_clock::now();
    for (std::uint64_t index = 0; index < requested_ticks; ++index) {
        total_moves += world.step().moved_cells;
    }
    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start);

    std::cout << "Meat2D headless simulation " << meat2d::version_string << '\n'
              << "protocol=" << meat2d::net::protocol_version
              << " max_players=" << static_cast<int>(meat2d::net::maximum_players) << '\n'
              << "ticks=" << world.current_tick() << " moves=" << total_moves
              << " elapsed_ms=" << std::fixed << std::setprecision(2)
              << elapsed.count() * 1000.0 << '\n'
              << "state_hash=0x" << std::hex << std::uppercase << world.state_hash()
              << std::dec << '\n';
    return 0;
}
