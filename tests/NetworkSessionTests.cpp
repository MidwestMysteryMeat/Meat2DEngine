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

void test_authoritative_client_server_session() {
    meat2d::net::AuthoritativeServer server({
        .world =
            {
                .width = 128,
                .height = 128,
                .seed = 91,
                .sleep_after_ticks = 30,
            },
        .port = 0,
        .tick_rate = 60,
        .maximum_clients = 2,
        .interest_radius_chunks = 1,
        .maximum_brush_radius = 8,
        .snapshot_interval_ticks = 1,
        .chunk_interval_ticks = 1,
        .client_timeout_updates = 100,
        .public_directory = std::nullopt,
    });
    const auto network_entity = server.scene().create_entity("Network Actor");
    check(server.scene().add_transform(network_entity, {.position = {12, 34}}) != nullptr &&
              server.scene().add_tag(network_entity, "replicated"),
          "server could not prepare a replicated scene entity");
    check(server.start(), "authoritative server failed to start");
    if (!server.running()) {
        return;
    }

    meat2d::net::AuthoritativeClient client;
    check(client.connect(
              {
                  .address = "localhost",
                  .port = server.port(),
              },
              "Session Test", 0xC0FFEEU),
          "authoritative client failed to start connecting");

    for (int update = 0; update < 80 && !client.connected(); ++update) {
        client.update();
        server.update();
        client.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(client.connected(), "client/server handshake did not complete");
    check(server.client_count() == 1, "server did not allocate exactly one client slot");
    check(client.client_id() == 1, "client received an invalid slot ID");
    check(client.welcome().has_value(), "client did not retain its welcome state");
    if (!client.connected()) {
        return;
    }

    check(client.paint({10, 10}, meat2d::MaterialId::Stone, 2),
          "connected client could not send a paint input");
    bool server_applied = false;
    bool client_replicated = false;
    bool scene_replicated = false;
    for (int update = 0; update < 120; ++update) {
        client.update();
        server.update();
        client.update();
        server_applied =
            server.simulation().world().material({10, 10}) == meat2d::MaterialId::Stone;
        const auto* mirror = client.replicated_world();
        client_replicated =
            mirror != nullptr && mirror->material({10, 10}) == meat2d::MaterialId::Stone;
        const auto* scene_mirror = client.replicated_scene();
        scene_replicated = scene_mirror != nullptr &&
                           scene_mirror->find_tagged("replicated").size() == 1U &&
                           scene_mirror->world_position(network_entity) == meat2d::Vec2i{12, 34};
        if (server_applied && client_replicated && scene_replicated && client.latest_snapshot()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(server_applied, "server did not apply validated client input");
    check(client_replicated, "interest-managed chunk delta did not reach client mirror");
    check(scene_replicated, "fragmented authoritative scene snapshot did not reach client mirror");
    check(client.latest_snapshot().has_value(), "client received no authoritative snapshot");

    for (int update = 0; update < 140; ++update) {
        client.update();
        server.update();
        client.update();
    }
    check(server.client_count() == 1, "active client timed out despite periodic keepalives");

    client.disconnect();
    for (int update = 0; update < 10 && server.client_count() != 0; ++update) {
        server.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(server.client_count() == 0, "server retained a disconnected client slot");
}

void test_prediction_and_reconciliation() {
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
        .maximum_clients = 2,
        .interest_radius_chunks = 1,
        .maximum_brush_radius = 8,
        .snapshot_interval_ticks = 1,
        .chunk_interval_ticks = 1,
        .client_timeout_updates = 100,
        .public_directory = std::nullopt,
    });
    check(server.start(), "prediction test server failed to start");
    if (!server.running()) {
        return;
    }

    meat2d::net::AuthoritativeClient client;
    check(client.connect(
              {
                  .address = "localhost",
                  .port = server.port(),
              },
              "Prediction Test", 0xFACADEU),
          "prediction test client failed to start connecting");
    for (int update = 0; update < 80 && !client.connected(); ++update) {
        client.update();
        server.update();
        client.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(client.connected(), "prediction test handshake did not complete");
    if (!client.connected()) {
        return;
    }

    check(client.pending_predictions() == 0, "client started with pending predictions");
    check(client.paint({40, 40}, meat2d::MaterialId::Stone, 3),
          "connected client could not send a predicted paint");
    const auto* mirror = client.replicated_world();
    check(mirror != nullptr && mirror->material({40, 40}) == meat2d::MaterialId::Stone,
          "paint was not predicted locally before server confirmation");
    check(client.pending_predictions() == 1, "predicted paint was not tracked");

    bool acknowledged = false;
    bool authoritative = false;
    bool replicated = false;
    meat2d::net::ClientUpdateStats accumulated{};
    for (int update = 0; update < 200; ++update) {
        const auto stats = client.update();
        accumulated.chunk_hash_mismatches += stats.chunk_hash_mismatches;
        server.update();
        client.update();
        acknowledged = client.pending_predictions() == 0 &&
                       client.acknowledged_input_sequence() != 0U;
        authoritative =
            server.simulation().world().material({40, 40}) == meat2d::MaterialId::Stone;
        replicated = mirror->material({40, 40}) == meat2d::MaterialId::Stone;
        if (acknowledged && authoritative && replicated && client.latest_snapshot()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(acknowledged, "server snapshot did not acknowledge the predicted input");
    check(authoritative, "server did not apply the predicted paint");
    check(replicated, "replica did not converge on the painted material");
    check(client.chunk_hash_mismatches() == 0 && accumulated.chunk_hash_mismatches == 0,
          "chunk hash diagnostics reported divergence on a healthy session");
    const auto snapshot = client.latest_snapshot();
    check(snapshot.has_value() && snapshot->state_hash != 0U,
          "snapshot did not carry a server state hash");

    client.disconnect();
}

void test_security_budget_disconnects_abusive_client() {
    meat2d::net::AuthoritativeServer server({
        .world =
            {
                .width = 64,
                .height = 64,
                .seed = 93,
                .sleep_after_ticks = 30,
            },
        .port = 0,
        .maximum_clients = 1,
        .snapshot_interval_ticks = 1000,
        .chunk_interval_ticks = 1000,
        .client_timeout_updates = 100,
        .public_directory = std::nullopt,
        .maximum_invalid_datagrams_per_client = 2,
        .security_window_updates = 60,
    });
    check(server.start(), "security budget test server failed to start");
    if (!server.running()) {
        return;
    }

    meat2d::net::AuthoritativeClient client;
    check(client.connect(
              {
                  .address = "localhost",
                  .port = server.port(),
              },
              "Security Budget Test", 0xBADBEEFU),
          "security budget test client failed to start connecting");
    for (int update = 0; update < 80 && !client.connected(); ++update) {
        client.update();
        server.update();
        client.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(client.connected(), "security budget test handshake did not complete");
    if (!client.connected()) {
        return;
    }

    check(client.send_input({
              .kind = meat2d::net::InputKind::Paint,
              .focus = {8, 8},
              .target = {8, 8},
              .material = meat2d::MaterialId::Stone,
              .radius = 255,
          }),
          "security budget test could not send its first invalid input");
    const auto first = server.update();
    check(first.security_rejections == 1U && first.security_disconnects == 0U,
          "server did not record the first invalid input against the security budget");

    check(client.send_input({
              .kind = meat2d::net::InputKind::Paint,
              .focus = {8, 8},
              .target = {8, 8},
              .material = meat2d::MaterialId::Stone,
              .radius = 255,
          }),
          "security budget test could not send its second invalid input");
    const auto second = server.update();
    check(second.security_rejections == 1U && second.security_disconnects == 1U &&
              server.client_count() == 0U,
          "server did not disconnect the client after its invalid-input budget was exhausted");
    client.disconnect();
}

void test_incompatible_client_build_is_rejected() {
    meat2d::net::AuthoritativeServer server({
        .world =
            {
                .width = 64,
                .height = 64,
                .seed = 94,
                .sleep_after_ticks = 30,
            },
        .port = 0,
        .maximum_clients = 1,
        .client_timeout_updates = 100,
        .public_directory = std::nullopt,
        .minimum_client_build_id = 7,
        .maximum_client_build_id = 9,
    });
    check(server.start(), "build compatibility test server failed to start");
    if (!server.running()) {
        return;
    }

    meat2d::net::AuthoritativeClient client;
    check(client.set_build_id(6), "client rejected a valid pre-connect build ID");
    check(client.build_id() == 6U, "client did not retain its configured build ID");
    check(client.connect(
              {
                  .address = "localhost",
                  .port = server.port(),
              },
              "Incompatible Build Test", 0xB17D1U),
          "incompatible build test client failed to start connecting");

    const auto stats = server.update();
    check(stats.incompatible_clients == 1U,
          "server did not report the incompatible client build");
    check(server.client_count() == 0U,
          "incompatible client was allocated an authoritative session slot");
    check(client.state() == meat2d::net::ClientConnectionState::Connecting,
          "incompatible client did not remain outside the connected state");
    check(!client.set_build_id(8), "client allowed its build ID to change while connecting");
    client.disconnect();
}

} // namespace meat2d_tests
