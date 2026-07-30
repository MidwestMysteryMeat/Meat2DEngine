#include "meat2d/core/Version.hpp"
#include "meat2d/net/Discovery.hpp"
#include "meat2d/net/Session.hpp"

#include <charconv>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

namespace {

struct Options {
    std::string host{"127.0.0.1"};
    std::string name{"Meat2D Remote"};
    std::string directory_host{"127.0.0.1"};
    std::uint16_t port{meat2d::net::default_port};
    std::uint16_t discovery_port{meat2d::net::default_lan_discovery_port};
    std::uint16_t directory_port{meat2d::net::default_directory_port};
    std::uint64_t server_id{};
    std::uint32_t updates{600};
    bool realtime{true};
    bool list_lan{};
    bool list_public{};
};

template <typename Integer> void parse_integer(std::string_view text, Integer& output) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), output);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        output = 0;
    }
}

Options parse_options(int argc, char** argv) {
    Options options{};
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--host" && index + 1 < argc) {
            options.host = argv[++index];
        } else if (argument == "--port" && index + 1 < argc) {
            parse_integer(std::string_view(argv[++index]), options.port);
        } else if (argument == "--name" && index + 1 < argc) {
            options.name = argv[++index];
        } else if (argument == "--list-lan") {
            options.list_lan = true;
        } else if (argument == "--list-public") {
            options.list_public = true;
        } else if (argument == "--discovery-port" && index + 1 < argc) {
            parse_integer(std::string_view(argv[++index]), options.discovery_port);
        } else if (argument == "--directory" && index + 1 < argc) {
            options.directory_host = argv[++index];
        } else if (argument == "--directory-port" && index + 1 < argc) {
            parse_integer(std::string_view(argv[++index]), options.directory_port);
        } else if (argument == "--server-id" && index + 1 < argc) {
            parse_integer(std::string_view(argv[++index]), options.server_id);
        } else if (argument == "--updates" && index + 1 < argc) {
            parse_integer(std::string_view(argv[++index]), options.updates);
        } else if (argument == "--fast") {
            options.realtime = false;
        }
    }
    return options;
}

void print_servers(std::span<const meat2d::net::ServerInfo> servers) {
    if (servers.empty()) {
        std::cout << "No compatible servers found\n";
        return;
    }
    for (const auto& server : servers) {
        std::cout << "id=" << server.server_id << " endpoint=" << server.endpoint.address << ':'
                  << server.endpoint.port << " players=" << static_cast<int>(server.current_players)
                  << '/' << static_cast<int>(server.maximum_clients) << " name=\"" << server.name
                  << "\" mode=\"" << server.mode << "\" map=\"" << server.map << "\""
                  << (server.nat_punch_available ? " punch=yes" : "") << '\n';
    }
}

int list_lan_servers(const Options& options) {
    meat2d::net::LanServerBrowser browser;
    if (!browser.refresh(options.discovery_port, 1)) {
        std::cerr << "LAN search failed: " << browser.last_error() << '\n';
        return 1;
    }
    for (int update = 0; update < 100; ++update) {
        browser.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    print_servers(browser.servers());
    return 0;
}

int list_public_servers(const Options& options) {
    meat2d::net::PublicServerBrowser browser;
    if (!browser.refresh(
            {
                .address = options.directory_host,
                .port = options.directory_port,
            },
            1)) {
        std::cerr << "Public search failed: " << browser.last_error() << '\n';
        return 1;
    }
    for (int update = 0; update < 200 && !browser.complete(); ++update) {
        browser.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!browser.complete()) {
        std::cerr << "Public directory did not answer\n";
        return 1;
    }
    print_servers(browser.servers());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const auto options = parse_options(argc, argv);
    if (options.list_lan) {
        return list_lan_servers(options);
    }
    if (options.list_public) {
        return list_public_servers(options);
    }

    meat2d::net::AuthoritativeClient client;
    const bool started = options.server_id == 0U ? client.connect(
                                                       {
                                                           .address = options.host,
                                                           .port = options.port,
                                                       },
                                                       options.name)
                                                 : client.connect_via_directory(
                                                       {
                                                           .address = options.directory_host,
                                                           .port = options.directory_port,
                                                       },
                                                       options.server_id, options.name);
    if (!started) {
        std::cerr << "Connection start failed: " << client.last_error() << '\n';
        return 1;
    }

    std::cout << "Meat2D remote " << meat2d::version_string << " connecting "
              << (options.server_id == 0U
                      ? "directly to " + options.host + ':' + std::to_string(options.port)
                      : "through directory " + options.directory_host + ':' +
                            std::to_string(options.directory_port) + " to server " +
                            std::to_string(options.server_id))
              << '\n';
    auto next_update = std::chrono::steady_clock::now();
    bool announced = false;
    for (std::uint32_t update = 0; options.updates == 0U || update < options.updates; ++update) {
        const auto stats = client.update();
        if (client.connected() && !announced) {
            announced = true;
            std::cout << "connected as client " << static_cast<int>(client.client_id()) << '\n';
        }
        if (client.latest_snapshot() && update % 60U == 0U) {
            std::cout << "server_tick=" << client.latest_snapshot()->server_tick
                      << " agents=" << client.latest_snapshot()->agent_count
                      << " organisms=" << client.latest_snapshot()->organism_population
                      << " chunks=" << stats.completed_chunks << " hash=0x" << std::hex
                      << std::uppercase << client.latest_snapshot()->state_hash << std::dec
                      << " acked_input=" << client.acknowledged_input_sequence()
                      << " hash_mismatches=" << client.chunk_hash_mismatches() << '\n';
        }
        if (client.state() == meat2d::net::ClientConnectionState::Rejected ||
            client.state() == meat2d::net::ClientConnectionState::TimedOut) {
            std::cerr << "Connection ended: " << client.last_error() << '\n';
            return 1;
        }
        if (options.realtime) {
            next_update += std::chrono::microseconds(1'000'000 / 60);
            std::this_thread::sleep_until(next_update);
        }
    }
    if (!client.connected()) {
        std::cerr << "Connection did not complete\n";
        return 1;
    }
    client.disconnect();
    return 0;
}
