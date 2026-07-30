#include "meat2d/core/Version.hpp"
#include "meat2d/net/Protocol.hpp"
#include "meat2d/net/Session.hpp"
#include "meat2d/sim/ChunkStore.hpp"
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
#include <string>
#include <string_view>
#include <thread>

namespace {

struct Options {
    std::uint64_t ticks{600};
    std::uint16_t port{meat2d::net::default_port};
    std::uint16_t discovery_port{meat2d::net::default_lan_discovery_port};
    std::uint16_t directory_port{meat2d::net::default_directory_port};
    std::string session_name{"Meat2D Server"};
    std::string mode_name{"Elements"};
    std::string map_name{"Elements Lab"};
    std::string directory_host;
    std::string persist_directory;
    bool ticks_explicit{};
    bool listen{};
    bool realtime{true};
    bool advertise_lan{true};
};

std::atomic_bool keep_running{true};

void stop_server(int) {
    keep_running = false;
}

template <typename Integer> bool parse_integer(std::string_view text, Integer& output) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), output);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

Options parse_options(int argc, char** argv) {
    Options options{};
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--listen") {
            options.listen = true;
        } else if (argument == "--lan") {
            options.advertise_lan = true;
        } else if (argument == "--no-lan") {
            options.advertise_lan = false;
        } else if (argument == "--fast") {
            options.realtime = false;
        } else if (argument == "--ticks" && index + 1 < argc) {
            options.ticks_explicit = parse_integer(argv[++index], options.ticks);
        } else if (argument == "--port" && index + 1 < argc) {
            parse_integer(argv[++index], options.port);
        } else if (argument == "--discovery-port" && index + 1 < argc) {
            parse_integer(argv[++index], options.discovery_port);
        } else if (argument == "--name" && index + 1 < argc) {
            options.session_name = argv[++index];
        } else if (argument == "--mode" && index + 1 < argc) {
            options.mode_name = argv[++index];
        } else if (argument == "--map" && index + 1 < argc) {
            options.map_name = argv[++index];
        } else if (argument == "--public-directory" && index + 1 < argc) {
            options.directory_host = argv[++index];
        } else if (argument == "--directory-port" && index + 1 < argc) {
            parse_integer(argv[++index], options.directory_port);
        } else if (argument == "--persist" && index + 1 < argc) {
            options.persist_directory = argv[++index];
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
    simulation.spawn_agent(meat2d::ai::AgentKind::Grazer, {world.width() / 4 - 7, floor_y - 1});
    simulation.spawn_agent(meat2d::ai::AgentKind::Predator, {world.width() / 2 - 12, floor_y - 1});
    simulation.spawn_agent(meat2d::ai::AgentKind::Worker, {world.width() * 2 / 3, floor_y - 1});
    simulation.organisms().seed({world.width() / 3, floor_y - 5},
                                meat2d::life::photosynthetic_genome, 1'300);
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
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start);

    std::cout << "Meat2D headless simulation " << meat2d::version_string << '\n'
              << "protocol=" << meat2d::net::protocol_version
              << " max_players=" << static_cast<int>(meat2d::net::maximum_players) << '\n'
              << "ticks=" << world.current_tick() << " moves=" << total_moves
              << " elapsed_ms=" << std::fixed << std::setprecision(2) << elapsed.count() * 1000.0
              << '\n'
              << "state_hash=0x" << std::hex << std::uppercase << world.state_hash() << std::dec
              << '\n';
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
        .session_name = options.session_name,
        .mode_name = options.mode_name,
        .map_name = options.map_name,
        .build_id = 1,
        .password_protected = false,
        .advertise_lan = options.advertise_lan,
        .lan_discovery_port = options.discovery_port,
        .advertise_public = !options.directory_host.empty(),
        .public_directory = options.directory_host.empty()
                                ? std::optional<meat2d::net::Endpoint>{}
                                : std::optional<meat2d::net::Endpoint>{meat2d::net::Endpoint{
                                      .address = options.directory_host,
                                      .port = options.directory_port,
                                  }},
        .directory_heartbeat_updates = 120,
    });
    seed_server_lab(server.simulation());
    if (!options.persist_directory.empty()) {
        const meat2d::ChunkStore store(options.persist_directory);
        if (store.has_chunk(0, 0)) {
            const auto loaded = store.load_all(server.simulation().world());
            std::cout << "Loaded persisted world: " << loaded << " chunks from "
                      << options.persist_directory << '\n';
        } else {
            std::cout << "No persisted world found at " << options.persist_directory
                      << ", starting fresh\n";
        }
    }
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
              << " tick_rate=60\n"
              << "server_id=" << server.server_id() << " LAN="
              << (options.advertise_lan
                      ? std::string("UDP ") + std::to_string(options.discovery_port)
                      : std::string("off"))
              << " public_directory="
              << (options.directory_host.empty()
                      ? std::string("off")
                      : options.directory_host + ':' + std::to_string(options.directory_port))
              << '\n';

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

    if (!options.persist_directory.empty()) {
        const meat2d::ChunkStore store(options.persist_directory);
        const auto saved = store.save_all(server.simulation().world());
        std::cout << "Saved persisted world: " << saved << " chunks to "
                  << options.persist_directory << '\n';
    }

    std::cout << "Server stopped at tick " << server.simulation().world().current_tick()
              << " hash=0x" << std::hex << std::uppercase << server.simulation().state_hash()
              << std::dec << '\n';
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const auto options = parse_options(argc, argv);
    return options.listen ? run_dedicated_server(options) : run_benchmark(options);
}
