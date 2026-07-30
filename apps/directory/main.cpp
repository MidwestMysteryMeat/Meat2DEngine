#include "meat2d/core/Version.hpp"
#include "meat2d/net/Discovery.hpp"

#include <atomic>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <thread>

namespace {

struct Options {
    std::uint16_t port{meat2d::net::default_directory_port};
    std::size_t maximum_servers{1'024};
    std::uint32_t lease_seconds{15};
};

std::atomic_bool keep_running{true};

void stop_directory(int) {
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
        if (argument == "--port" && index + 1 < argc) {
            parse_integer(std::string_view(argv[++index]), options.port);
        } else if (argument == "--max-servers" && index + 1 < argc) {
            parse_integer(std::string_view(argv[++index]), options.maximum_servers);
        } else if (argument == "--lease-seconds" && index + 1 < argc) {
            parse_integer(std::string_view(argv[++index]), options.lease_seconds);
        }
    }
    return options;
}

} // namespace

int main(int argc, char** argv) {
    const auto options = parse_options(argc, argv);
    meat2d::net::PublicDirectoryServer directory({
        .port = options.port,
        .maximum_servers = options.maximum_servers,
        .maximum_datagrams_per_update = 512,
        .lease_timeout = std::chrono::seconds(options.lease_seconds),
    });
    if (!directory.start()) {
        std::cerr << "Directory start failed: " << directory.last_error() << '\n';
        return 1;
    }

    std::signal(SIGINT, stop_directory);
    std::signal(SIGTERM, stop_directory);
    std::cout << "Meat2D public directory " << meat2d::version_string << " listening on UDP port "
              << directory.port() << '\n'
              << "This service lists endpoints and coordinates NAT punching; "
                 "it does not relay gameplay.\n";

    auto next_update = std::chrono::steady_clock::now();
    std::uint64_t updates = 0;
    std::uint64_t registrations = 0;
    std::uint64_t lists = 0;
    std::uint64_t joins = 0;
    while (keep_running) {
        const auto stats = directory.update();
        registrations += stats.registrations;
        lists += stats.list_requests;
        joins += stats.join_requests;
        ++updates;
        if (updates % 600U == 0U) {
            std::cout << "servers=" << directory.server_count()
                      << " registrations=" << registrations << " lists=" << lists
                      << " joins=" << joins << '\n';
        }
        next_update += std::chrono::microseconds(1'000'000 / 60);
        std::this_thread::sleep_until(next_update);
    }
    std::cout << "Directory stopped\n";
    return 0;
}
