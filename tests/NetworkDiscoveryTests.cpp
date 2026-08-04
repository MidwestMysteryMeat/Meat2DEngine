#include "meat2d/ai/LivingSimulation.hpp"
#include "meat2d/ai/Crowd.hpp"
#include "meat2d/ai/LearningAgent.hpp"
#include "meat2d/ai/LearningEnvironment.hpp"
#include "meat2d/ai/NeuralNetwork.hpp"
#include "meat2d/assets/Animation.hpp"
#include "meat2d/assets/SpriteSheet.hpp"
#include "meat2d/assets/TileMap.hpp"
#include "meat2d/assets/TextureAtlas.hpp"
#include "meat2d/core/DeterministicRng.hpp"
#include "meat2d/core/FixedTimestep.hpp"
#include "meat2d/input/Input.hpp"
#include "meat2d/net/ChunkCodec.hpp"
#include "meat2d/net/Discovery.hpp"
#include "meat2d/net/Fragmentation.hpp"
#include "meat2d/net/PacketCodec.hpp"
#include "meat2d/net/Protocol.hpp"
#include "meat2d/net/Reliability.hpp"
#include "meat2d/net/Session.hpp"
#include "meat2d/net/UdpSocket.hpp"
#include "meat2d/replay/Replay.hpp"
#include "meat2d/render/Camera.hpp"
#include "meat2d/render/DebugDraw.hpp"
#include "meat2d/render/Particles.hpp"
#include "meat2d/render/SpriteBatch.hpp"
#include "meat2d/render/StaticMeshBatch.hpp"
#include "meat2d/render/WorldView.hpp"
#include "meat2d/scene/Physics.hpp"
#include "meat2d/scene/Scene.hpp"
#include "meat2d/scene/SceneHistory.hpp"
#include "meat2d/scene/SceneSnapshot.hpp"
#include "meat2d/scene/SceneStack.hpp"
#include "meat2d/sim/ChunkStore.hpp"
#include "meat2d/sim/Projectile.hpp"
#include "meat2d/sim/Scenario.hpp"
#include "meat2d/sim/World.hpp"
#include "meat2d/tools/ProjectBrowser.hpp"
#include "meat2d/tools/ProjectManager.hpp"
#include "meat2d/tools/SceneEditor.hpp"
#include "meat2d/tools/McpGateway.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include "TestSupport.hpp"

namespace meat2d_tests {

std::optional<meat2d::net::Datagram> wait_for_datagram(meat2d::net::UdpSocket& socket) {
    for (int attempt = 0; attempt < 200; ++attempt) {
        if (auto datagram = socket.receive()) {
            return datagram;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return std::nullopt;
}

void test_udp_loopback() {
    meat2d::net::UdpSocket server;
    meat2d::net::UdpSocket client;
    check(server.open(), "loopback server socket did not open");
    check(client.open(), "loopback client socket did not open");
    if (!server.valid() || !client.valid()) {
        return;
    }

    const std::array<std::uint8_t, 5> outbound{4, 8, 15, 16, 23};
    const bool sent = client.send(
        {
            .address = "127.0.0.1",
            .port = server.local_port(),
        },
        outbound);
    check(sent, "UDP loopback send failed");
    const auto received = wait_for_datagram(server);
    check(received.has_value(), "UDP loopback server received no datagram");
    check(received &&
              received->bytes == std::vector<std::uint8_t>(outbound.begin(), outbound.end()),
          "UDP loopback payload was corrupted");
    if (!received) {
        return;
    }

    const std::array<std::uint8_t, 3> reply{42, 43, 44};
    check(server.send(received->sender, reply), "UDP loopback reply failed");
    const auto returned = wait_for_datagram(client);
    check(returned.has_value(), "UDP loopback client received no reply");
    check(returned && returned->bytes == std::vector<std::uint8_t>(reply.begin(), reply.end()),
          "UDP loopback reply was corrupted");
}

void test_discovery_codec() {
    const meat2d::net::ServerInfo server{
        .server_id = 0x1122334455667788ULL,
        .endpoint =
            {
                .address = "203.0.113.42",
                .port = 27182,
            },
        .name = "The Meat Locker",
        .mode = "Falling Sand",
        .map = "Volcanic Lab",
        .build_id = 7,
        .current_players = 3,
        .maximum_clients = 8,
        .password_protected = true,
        .nat_punch_available = true,
    };
    check(meat2d::net::valid_server_info(server), "valid server listing metadata was rejected");

    const auto announcement_payload = meat2d::net::encode_lan_announcement({
        .request_id = 99,
        .server = server,
    });
    check(announcement_payload.has_value(), "LAN announcement did not encode");
    const auto announcement = announcement_payload
                                  ? meat2d::net::decode_lan_announcement(*announcement_payload)
                                  : std::nullopt;
    check(announcement && announcement->server == server && announcement->request_id == 99,
          "LAN announcement changed during serialization");

    meat2d::net::DirectoryListResponseMessage page{
        .request_id = 101,
        .next_cursor = meat2d::net::directory_end_cursor,
        .servers = {},
    };
    for (std::size_t index = 0; index < meat2d::net::maximum_directory_page_entries; ++index) {
        auto entry = server;
        entry.server_id += index;
        page.servers.push_back(std::move(entry));
    }
    const auto page_payload = meat2d::net::encode_directory_list_response(page);
    check(page_payload.has_value(), "full directory page did not encode");
    const auto decoded_page =
        page_payload ? meat2d::net::decode_directory_list_response(*page_payload) : std::nullopt;
    check(decoded_page && decoded_page->servers == page.servers,
          "directory page changed during serialization");

    page.servers.push_back(server);
    check(!meat2d::net::encode_directory_list_response(page).has_value(),
          "oversized directory page was accepted");

    auto truncated = announcement_payload.value_or(std::vector<std::uint8_t>{});
    if (!truncated.empty()) {
        truncated.pop_back();
    }
    check(!meat2d::net::decode_lan_announcement(truncated).has_value(),
          "truncated LAN announcement was accepted");

    auto invalid = server;
    invalid.current_players = 9;
    check(!meat2d::net::valid_server_info(invalid), "listing with too many players was accepted");
}

void test_lan_discovery() {
    meat2d::net::LanServerAdvertiser advertiser;
    check(advertiser.start(0), "LAN advertiser did not start");
    if (!advertiser.running()) {
        return;
    }
    const meat2d::net::ServerInfo expected{
        .server_id = 5001,
        .endpoint =
            {
                .address = "0.0.0.0",
                .port = 31001,
            },
        .name = "LAN Test",
        .mode = "Sandbox",
        .map = "Test Lab",
        .build_id = 1,
        .current_players = 1,
        .maximum_clients = 4,
    };
    meat2d::net::LanServerBrowser browser;
    check(browser.refresh(advertiser.port(), 1), "LAN browser could not broadcast a query");
    for (int update = 0; update < 200 && browser.servers().empty(); ++update) {
        advertiser.update(expected);
        browser.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(browser.servers().size() == 1, "LAN browser did not discover the local server");
    if (!browser.servers().empty()) {
        check(browser.servers().front().server_id == expected.server_id &&
                  browser.servers().front().endpoint.port == expected.endpoint.port &&
                  browser.servers().front().endpoint.address != "0.0.0.0" &&
                  !browser.servers().front().endpoint.address.empty(),
              "LAN browser reported the wrong join endpoint");
    }
}

void test_public_directory_session() {
    meat2d::net::PublicDirectoryServer directory({
        .port = 0,
        .maximum_servers = 32,
        .maximum_datagrams_per_update = 256,
        .lease_timeout = std::chrono::milliseconds(500),
    });
    check(directory.start(), "public directory did not start");
    if (!directory.running()) {
        return;
    }

    meat2d::net::AuthoritativeServer server({
        .world =
            {
                .width = 128,
                .height = 128,
                .seed = 92,
                .sleep_after_ticks = 30,
            },
        .port = 0,
        .tick_rate = 60,
        .maximum_clients = 4,
        .interest_radius_chunks = 1,
        .maximum_brush_radius = 8,
        .maximum_inputs_per_update = 4,
        .snapshot_interval_ticks = 1,
        .chunk_interval_ticks = 1,
        .client_timeout_updates = 100,
        .session_name = "Public Session Test",
        .mode_name = "Elements",
        .map_name = "Directory Lab",
        .build_id = 1,
        .password_protected = false,
        .advertise_lan = false,
        .lan_discovery_port = meat2d::net::default_lan_discovery_port,
        .advertise_public = true,
        .public_directory =
            meat2d::net::Endpoint{
                .address = "127.0.0.1",
                .port = directory.port(),
            },
        .directory_heartbeat_updates = 1,
    });
    check(server.start(), "public authoritative server did not start");
    if (!server.running()) {
        return;
    }
    server.update();
    directory.update();
    check(directory.server_count() == 1, "directory did not retain the server heartbeat");

    meat2d::net::PublicServerBrowser browser;
    check(browser.refresh(
              {
                  .address = "localhost",
                  .port = directory.port(),
              },
              1),
          "public browser could not request a server list");
    for (int update = 0; update < 100 && !browser.complete(); ++update) {
        directory.update();
        browser.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(browser.complete(), "public server list did not complete");
    check(browser.servers().size() == 1,
          "public server list returned an unexpected number of servers");
    if (!browser.servers().empty()) {
        check(browser.servers().front().server_id == server.server_id() &&
                  browser.servers().front().endpoint.port == server.port() &&
                  browser.servers().front().endpoint.address == "127.0.0.1",
              "directory did not report the observed public endpoint");
    }

    meat2d::net::AuthoritativeClient client;
    check(client.connect_via_directory(
              {
                  .address = "127.0.0.1",
                  .port = directory.port(),
              },
              server.server_id(), "Directory Client", 0xD1EC70U),
          "directory-assisted client could not start");
    for (int update = 0; update < 200 && !client.connected(); ++update) {
        client.update();
        directory.update();
        server.update();
        client.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(client.connected(), "directory-assisted NAT punch and handshake did not complete");
    check(server.client_count() == 1, "directory-assisted join did not allocate one server slot");
    client.disconnect();
}

void test_directory_pagination_identity_and_expiry() {
    meat2d::net::PublicDirectoryServer directory({
        .port = 0,
        .maximum_servers = 32,
        .maximum_datagrams_per_update = 256,
        .lease_timeout = std::chrono::milliseconds(120),
    });
    meat2d::net::UdpSocket host_socket;
    meat2d::net::UdpSocket attacker_socket;
    check(directory.start(), "expiry-test directory did not start");
    check(host_socket.open(), "directory test host socket did not open");
    check(attacker_socket.open(), "directory test attacker socket did not open");
    if (!directory.running() || !host_socket.valid() || !attacker_socket.valid()) {
        return;
    }
    const meat2d::net::Endpoint directory_endpoint{
        .address = "127.0.0.1",
        .port = directory.port(),
    };
    const auto send_registration = [&](meat2d::net::UdpSocket& socket, std::uint64_t server_id,
                                       std::uint64_t secret, std::string name) {
        const auto payload = meat2d::net::encode_directory_registration({
            .registration_secret = secret,
            .server =
                {
                    .server_id = server_id,
                    .endpoint =
                        {
                            .address = "0.0.0.0",
                            .port = socket.local_port(),
                        },
                    .name = std::move(name),
                    .mode = "Pagination",
                    .map = "Directory Test",
                    .build_id = 1,
                    .current_players = 0,
                    .maximum_clients = 8,
                },
        });
        meat2d::net::PacketHeader header{};
        header.type = meat2d::net::PacketType::DirectoryRegister;
        const auto datagram = payload ? meat2d::net::encode_packet(header, *payload) : std::nullopt;
        return datagram && socket.send(directory_endpoint, *datagram);
    };

    for (std::uint64_t index = 0; index < 8; ++index) {
        check(send_registration(host_socket, 7'000U + index, 17'000U + index,
                                "Page Server " + std::to_string(index)),
              "directory pagination registration did not send");
    }
    directory.update();
    check(directory.server_count() == 8, "directory did not retain all paginated registrations");

    check(send_registration(attacker_socket, 7'000U, 999'999U, "Hijacked Name"),
          "spoofed directory registration did not reach the directory");
    const auto attack_stats = directory.update();
    check(attack_stats.invalid_datagrams == 1,
          "directory did not reject a server-ID registration secret mismatch");

    meat2d::net::PublicServerBrowser browser;
    check(browser.refresh(directory_endpoint, 1), "pagination browser could not start");
    for (int update = 0; update < 100 && !browser.complete(); ++update) {
        directory.update();
        browser.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(browser.complete(), "paginated directory list did not finish");
    check(browser.servers().size() == 8, "paginated directory list lost or duplicated servers");
    const auto first = std::find_if(
        browser.servers().begin(), browser.servers().end(),
        [](const meat2d::net::ServerInfo& server) { return server.server_id == 7'000U; });
    check(first != browser.servers().end() && first->name == "Page Server 0",
          "spoofed registration replaced legitimate listing metadata");

    std::this_thread::sleep_for(std::chrono::milliseconds(140));
    const auto expiry_stats = directory.update();
    check(expiry_stats.expired_servers == 8 && directory.server_count() == 0,
          "stale public directory leases did not expire");
}

void test_public_browser_distrusts_directory_results() {
    meat2d::net::UdpSocket fake_directory;
    check(fake_directory.open(), "fake public directory socket did not open");
    if (!fake_directory.valid()) {
        return;
    }

    const auto directory_endpoint = meat2d::net::Endpoint{
        .address = "127.0.0.1",
        .port = fake_directory.local_port(),
    };
    meat2d::net::PublicServerBrowser browser;
    check(browser.refresh(directory_endpoint, 7),
          "public browser could not query the fake directory");
    const auto request_datagram = wait_for_datagram(fake_directory);
    const auto request_packet =
        request_datagram ? meat2d::net::decode_packet(request_datagram->bytes) : std::nullopt;
    const auto request = request_packet
                             ? meat2d::net::decode_directory_list_request(request_packet->payload)
                             : std::nullopt;
    check(request.has_value(), "fake directory received no valid list request");
    if (!request || !request_datagram) {
        return;
    }

    const auto incompatible_payload = meat2d::net::encode_directory_list_response({
        .request_id = request->request_id,
        .next_cursor = meat2d::net::directory_end_cursor,
        .servers =
            {
                {
                    .server_id = 88,
                    .endpoint =
                        {
                            .address = "127.0.0.1",
                            .port = 27182,
                        },
                    .name = "Wrong Build",
                    .mode = "Test",
                    .map = "Test",
                    .build_id = 8,
                    .current_players = 0,
                    .maximum_clients = 8,
                },
            },
    });
    meat2d::net::PacketHeader header{};
    header.type = meat2d::net::PacketType::DirectoryListResponse;
    const auto incompatible_datagram =
        incompatible_payload ? meat2d::net::encode_packet(header, *incompatible_payload)
                             : std::nullopt;
    check(incompatible_datagram &&
              fake_directory.send(request_datagram->sender, *incompatible_datagram),
          "fake directory could not send an incompatible listing");
    browser.update();
    check(browser.complete() && browser.servers().empty(),
          "public browser trusted an incompatible directory listing");

    meat2d::net::PublicServerBrowser looping_browser;
    check(looping_browser.refresh(directory_endpoint, 7),
          "pagination-loop browser could not query the fake directory");
    const auto looping_request_datagram = wait_for_datagram(fake_directory);
    const auto looping_request_packet =
        looping_request_datagram ? meat2d::net::decode_packet(looping_request_datagram->bytes)
                                 : std::nullopt;
    const auto looping_request =
        looping_request_packet
            ? meat2d::net::decode_directory_list_request(looping_request_packet->payload)
            : std::nullopt;
    if (!looping_request || !looping_request_datagram) {
        check(false, "fake directory received no pagination-loop request");
        return;
    }
    const auto looping_payload = meat2d::net::encode_directory_list_response({
        .request_id = looping_request->request_id,
        .next_cursor = 0,
        .servers = {},
    });
    const auto looping_datagram =
        looping_payload ? meat2d::net::encode_packet(header, *looping_payload) : std::nullopt;
    check(looping_datagram &&
              fake_directory.send(looping_request_datagram->sender, *looping_datagram),
          "fake directory could not send a looping page");
    looping_browser.update();
    check(looping_browser.complete() && !looping_browser.last_error().empty(),
          "public browser followed a non-advancing page cursor");
}

} // namespace meat2d_tests

