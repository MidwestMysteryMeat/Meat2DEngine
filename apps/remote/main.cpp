#include "meat2d/core/Version.hpp"
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
    std::uint16_t port{meat2d::net::default_port};
    std::uint32_t updates{600};
    bool realtime{true};
};

template <typename Integer>
void parse_integer(std::string_view text, Integer& output) {
    const auto result =
        std::from_chars(text.data(), text.data() + text.size(), output);
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
        } else if (argument == "--updates" && index + 1 < argc) {
            parse_integer(std::string_view(argv[++index]), options.updates);
        } else if (argument == "--fast") {
            options.realtime = false;
        }
    }
    return options;
}

} // namespace

int main(int argc, char** argv) {
    const auto options = parse_options(argc, argv);
    meat2d::net::AuthoritativeClient client;
    if (!client.connect(
            {
                .address = options.host,
                .port = options.port,
            },
            options.name)) {
        std::cerr << "Connection start failed: " << client.last_error() << '\n';
        return 1;
    }

    std::cout << "Meat2D remote " << meat2d::version_string << " connecting to "
              << options.host << ':' << options.port << '\n';
    auto next_update = std::chrono::steady_clock::now();
    bool announced = false;
    for (std::uint32_t update = 0;
         options.updates == 0U || update < options.updates;
         ++update) {
        const auto stats = client.update();
        if (client.connected() && !announced) {
            announced = true;
            std::cout << "connected as client " << static_cast<int>(client.client_id())
                      << '\n';
        }
        if (client.latest_snapshot() && update % 60U == 0U) {
            std::cout << "server_tick=" << client.latest_snapshot()->server_tick
                      << " agents=" << client.latest_snapshot()->agent_count
                      << " organisms="
                      << client.latest_snapshot()->organism_population
                      << " chunks=" << stats.completed_chunks << " hash=0x"
                      << std::hex << std::uppercase
                      << client.latest_snapshot()->state_hash << std::dec << '\n';
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
