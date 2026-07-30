#include "meat2d/core/Version.hpp"
#include "meat2d/net/Protocol.hpp"
#include "meat2d/net/Session.hpp"
#include "meat2d/sim/Scenario.hpp"
#include "meat2d/sim/World.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <thread>

namespace {

struct Options {
    std::uint64_t ticks{600};
    std::uint16_t port{meat2d::net::default_port};
    bool ticks_explicit{};
    bool listen{};
    bool realtime{true};
};

std::atomic_bool keep_running{true};

void stop_server(int) {
    keep_running = false;
}

template <typename Integer>
bool parse_integer(std::string_view text, Integer& output) {
    const auto result =
        std::from_chars(text.data(), text.data() + text.size(), output);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

Options parse_options(int argc, char** argv) {
    Options options{};
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--listen") {
            options.listen = true;
        } else if (argument == "--fast") {
            options.realtime = false;
        } else if (argument == "--ticks" && index + 1 < argc) {
            options.ticks_explicit = parse_integer(argv[++index], options.ticks);
        } else if (argument == "--port" && index + 1 < argc) {
            parse_integer(argv[++index], options.port);
        }
    }
    if (options.listen && !options.ticks_explicit) {
        options.ticks = 0;
    }
    return options;
}

void seed_server_lab(meat2d::ai::LivingSimulation& simulation) {
    auto& world = simulation.world();
    meat2d::seed_elements_lab(world);
    const auto floor_y = world.height() - std::max(4, world.height() / 18);
    for (int x = world.width() / 4; x < world.width() * 2 / 5; x += 12) {
        world.set_material({x, floor_y - 1}, meat2d::MaterialId::Plant);
    }
    for (int x = world.width() * 7 / 10; x < world.width() * 4 / 5; x += 4) {
        world.set_material({x, floor_y - 1}, meat2d::MaterialId::Debris);
    }
    simulation.spawn_agent(
        meat2d::ai::AgentKind::Grazer,
        {world.width() / 4 - 7, floor_y - 1});
    simulation.spawn_agent(
        meat2d::ai::AgentKind::Predator,
        {world.width() / 2 - 12, floor_y - 1});
    simulation.spawn_agent(
        meat2d::ai::AgentKind::Worker,
        {world.width() * 2 / 3, floor_y - 1});
    simulation.organisms().seed(
        {world.width() / 3, floor_y - 5},
        meat2d::life::photosynthetic_genome,
        1'300);
}

int run_benchmark(const Options& options) {
    meat2d::World world({
        .width = 320,
        .height = 180,
        .seed = 0x4D4541543244ULL,
        .sleep_after_ticks = 30,
    });
    meat2d::seed_sand_lab(world);

    std::uint64_t total_moves = 0;
    const auto start = std::chrono::steady_clock::now();
    for (std::uint64_t index = 0; index < options.ticks; ++index) {
        total_moves += world.step().moved_cells;
    }
    const auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start);

    std::cout << "Meat2D headless simulation " << meat2d::version_string << '\n'
              << "protocol=" << meat2d::net::protocol_version
              << " max_players=" << static_cast<int>(meat2d::net::maximum_players)
              << '\n'
              << "ticks=" << world.current_tick() << " moves=" << total_moves
              << " elapsed_ms=" << std::fixed << std::setprecision(2)
              << elapsed.count() * 1000.0 << '\n'
              << "state_hash=0x" << std::hex << std::uppercase
              << world.state_hash() << std::dec << '\n';
    return 0;
}

int run_dedicated_server(const Options& options) {
    meat2d::net::AuthoritativeServer server({
        .world =
            {
                .width = 320,
                .height = 180,
                .seed = 0x4D4541543244ULL,
                .sleep_after_ticks = 30,
            },
        .port = options.port,
        .tick_rate = 60,
        .maximum_clients = meat2d::net::maximum_players,
        .interest_radius_chunks = 2,
        .maximum_brush_radius = 8,
        .maximum_inputs_per_update = 4,
        .snapshot_interval_ticks = 3,
        .chunk_interval_ticks = 1,
        .client_timeout_updates = 600,
    });
    seed_server_lab(server.simulation());
    if (!server.start()) {
        std::cerr << "Server start failed: " << server.last_error() << '\n';
        return 1;
    }

    std::signal(SIGINT, stop_server);
    std::signal(SIGTERM, stop_server);
    std::cout << "Meat2D authoritative server " << meat2d::version_string
              << " listening on UDP port " << server.port() << '\n'
              << "protocol=" << meat2d::net::protocol_version
              << " max_players=" << static_cast<int>(meat2d::net::maximum_players)
              << " tick_rate=60\n";

    auto next_tick = std::chrono::steady_clock::now();
    std::uint64_t updates = 0;
    while (keep_running && (options.ticks == 0U || updates < options.ticks)) {
        const auto stats = server.update();
        ++updates;
        if (updates % 60U == 0U) {
            std::cout << "tick=" << server.simulation().world().current_tick()
                      << " clients=" << stats.connected_clients
                      << " agents=" << server.simulation().agents().size()
                      << " organisms=" << server.simulation().organisms().population()
                      << " packets_in=" << stats.datagrams_received
                      << " packets_out=" << stats.datagrams_sent << '\n';
        }
        if (options.realtime) {
            next_tick += std::chrono::microseconds(1'000'000 / 60);
            std::this_thread::sleep_until(next_tick);
        }
    }

    std::cout << "Server stopped at tick "
              << server.simulation().world().current_tick() << " hash=0x"
              << std::hex << std::uppercase << server.simulation().state_hash()
              << std::dec << '\n';
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const auto options = parse_options(argc, argv);
    return options.listen ? run_dedicated_server(options) : run_benchmark(options);
}
