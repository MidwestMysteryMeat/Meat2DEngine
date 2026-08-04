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

void test_packet_codec() {
    const meat2d::net::HelloMessage hello{
        .client_nonce = 0x123456789ABCDEF0ULL,
        .build_id = 0x01020304U,
        .player_name = "Mystery Meat",
    };
    const auto hello_payload = meat2d::net::encode_hello(hello);
    check(hello_payload.has_value(), "valid hello payload did not encode");

    meat2d::net::PacketHeader header{};
    header.type = meat2d::net::PacketType::Hello;
    header.flags = meat2d::net::PacketFlagReliable;
    header.sequence = 42;
    header.acknowledgement = 39;
    header.acknowledgement_bits = 5;
    header.server_tick = 900;
    const auto datagram = meat2d::net::encode_packet(header, *hello_payload);
    check(datagram.has_value(), "valid packet did not encode");
    check(datagram->size() <= meat2d::net::maximum_datagram_bytes,
          "encoded packet exceeded the safe datagram budget");

    const auto packet = meat2d::net::decode_packet(*datagram);
    check(packet.has_value(), "encoded packet did not decode");
    check(packet && packet->header.sequence == header.sequence,
          "packet sequence changed during serialization");
    check(packet && packet->header.acknowledgement_bits == header.acknowledgement_bits,
          "packet acknowledgement window changed during serialization");
    const auto decoded_hello = packet ? meat2d::net::decode_hello(packet->payload) : std::nullopt;
    check(decoded_hello.has_value(), "hello message did not decode");
    check(decoded_hello && decoded_hello->player_name == hello.player_name,
          "player name changed during serialization");
    check(decoded_hello && decoded_hello->client_nonce == hello.client_nonce,
          "client nonce changed during serialization");

    auto truncated = *datagram;
    truncated.pop_back();
    check(!meat2d::net::decode_packet(truncated).has_value(), "truncated datagram was accepted");
    auto bad_magic = *datagram;
    bad_magic[0] ^= 0xFFU;
    check(!meat2d::net::decode_packet(bad_magic).has_value(),
          "datagram with invalid protocol magic was accepted");
    auto bad_flags = *datagram;
    bad_flags[7] = 0x80U;
    check(!meat2d::net::decode_packet(bad_flags).has_value(),
          "datagram with unknown packet flags was accepted");

    const meat2d::net::InputMessage input{
        .session_token = 0xDEADBEEFCAFEBABEULL,
        .input_sequence = 17,
        .target_tick = 123,
        .kind = meat2d::net::InputKind::Paint,
        .focus = {320, 180},
        .target = {-12, 44},
        .material = meat2d::MaterialId::Lava,
        .radius = 8,
    };
    const auto decoded_input = meat2d::net::decode_input(meat2d::net::encode_input(input));
    check(decoded_input.has_value(), "input message did not decode");
    check(decoded_input && decoded_input->target == input.target &&
              decoded_input->material == input.material &&
              decoded_input->session_token == input.session_token,
          "input message changed during serialization");

    const auto snapshot_message = meat2d::net::decode_snapshot(meat2d::net::encode_snapshot({
        .server_tick = 777,
        .state_hash = 0xABCDEF0123456789ULL,
        .acknowledged_input_sequence = 41,
        .organism_population = 5,
        .agent_count = 3,
        .active_chunks = 2,
    }));
    check(snapshot_message.has_value() &&
              snapshot_message->acknowledged_input_sequence == 41 &&
              snapshot_message->state_hash == 0xABCDEF0123456789ULL,
          "snapshot message changed during serialization");

    const meat2d::net::SceneSnapshotMessage scene_snapshot_message{
        .state_hash = 0x1020304050607080ULL,
        .bytes = {0x4D, 0x32, 0x53, 0x43, 0x01},
    };
    const auto decoded_scene_snapshot = meat2d::net::decode_scene_snapshot(
        meat2d::net::encode_scene_snapshot(scene_snapshot_message));
    check(decoded_scene_snapshot &&
              decoded_scene_snapshot->state_hash == scene_snapshot_message.state_hash &&
              decoded_scene_snapshot->bytes == scene_snapshot_message.bytes,
          "scene snapshot network payload changed during serialization");

    const auto oversized_welcome = meat2d::net::decode_welcome(meat2d::net::encode_welcome({
        .client_nonce = 1,
        .session_token = 2,
        .world_seed = 3,
        .server_tick = 4,
        .world_width = meat2d::net::maximum_network_world_dimension,
        .world_height = meat2d::net::maximum_network_world_dimension,
        .tick_rate = 60,
        .client_id = 1,
        .maximum_clients = 2,
    }));
    check(!oversized_welcome.has_value(),
          "welcome message could request an unsafe client allocation");
}

void test_reliable_sequence_window() {
    meat2d::net::AcknowledgementTracker tracker;
    check(tracker.observe(100), "first sequence was rejected");
    check(tracker.observe(102), "newer sequence was rejected");
    check(tracker.observe(101), "out-of-order sequence inside the window was rejected");
    check(!tracker.observe(101), "duplicate sequence was accepted");
    check(tracker.acknowledgement() == 102, "latest acknowledgement is incorrect");
    check(meat2d::net::sequence_acknowledged(100, tracker.acknowledgement(),
                                             tracker.acknowledgement_bits()),
          "acknowledgement bits lost an older sequence");
    check(meat2d::net::sequence_acknowledged(101, tracker.acknowledgement(),
                                             tracker.acknowledgement_bits()),
          "acknowledgement bits lost an out-of-order sequence");

    meat2d::net::AcknowledgementTracker wrapped;
    wrapped.observe(std::numeric_limits<std::uint32_t>::max());
    check(wrapped.observe(0), "sequence wraparound was not treated as newer");
    check(wrapped.acknowledgement() == 0, "wrapped acknowledgement is incorrect");

    meat2d::net::ReliableChannel sender({
        .resend_after_updates = 2,
        .maximum_attempts = 6,
        .maximum_pending_packets = 16,
    });
    meat2d::net::ReliableChannel receiver;
    const std::array<std::uint8_t, 3> payload{1, 2, 3};
    const auto initial = sender.make_packet(meat2d::net::PacketType::Welcome, payload, 0, 0, true);
    check(initial.header.sequence == 1, "reliable sequence did not start at one");
    check(sender.pending_packets() == 1, "reliable packet was not retained");

    auto retransmissions = sender.collect_retransmissions(2, 2);
    check(retransmissions.size() == 1, "lost packet was not retransmitted");
    check(receiver.receive(retransmissions.front().header), "first retransmission was rejected");

    retransmissions = sender.collect_retransmissions(4, 4);
    check(retransmissions.size() == 1, "unacknowledged packet stopped retransmitting");
    check(!receiver.receive(retransmissions.front().header),
          "duplicate retransmission was accepted twice");
    const auto acknowledgement = receiver.make_acknowledgement(4, 4);
    sender.receive(acknowledgement.header);
    check(sender.pending_packets() == 0, "acknowledged packet remained pending");
    check(sender.stats().retransmissions == 2, "retransmission statistics are incorrect");
    check(receiver.stats().duplicates_received == 1, "duplicate receive statistics are incorrect");
}

void test_chunk_delta_fragmentation() {
    meat2d::World source({
        .width = 128,
        .height = 128,
        .seed = 90,
        .sleep_after_ticks = 30,
    });
    for (int y = 0; y < meat2d::chunk_size; ++y) {
        for (int x = 0; x < meat2d::chunk_size; ++x) {
            source.set_material({x, y}, ((x + y) & 1) == 0 ? meat2d::MaterialId::Stone
                                                           : meat2d::MaterialId::Wood);
        }
    }

    const auto encoded = meat2d::net::encode_chunk_delta(source, 0);
    check(encoded.has_value(), "valid chunk delta did not encode");
    check(encoded && encoded->size() > meat2d::net::maximum_fragment_data_bytes,
          "large chunk delta did not exercise fragmentation");

    const auto fragments = encoded ? meat2d::net::fragment_payload(77, *encoded)
                                   : std::vector<std::vector<std::uint8_t>>{};
    check(fragments.size() > 1, "chunk delta was not split into MTU-safe fragments");
    for (const auto& fragment : fragments) {
        check(fragment.size() + meat2d::net::encoded_header_bytes <=
                  meat2d::net::maximum_datagram_bytes,
              "fragment exceeded the datagram budget");
    }

    meat2d::net::FragmentAssembler assembler;
    std::optional<std::vector<std::uint8_t>> completed;
    if (!fragments.empty()) {
        assembler.accept(fragments.back(), 1);
    }
    for (auto fragment = fragments.rbegin(); fragment != fragments.rend(); ++fragment) {
        if (const auto result = assembler.accept(*fragment, 2)) {
            completed = result;
        }
    }
    check(completed.has_value(), "out-of-order fragments did not reassemble");
    check(completed && encoded && *completed == *encoded, "reassembled chunk was corrupted");

    meat2d::World target({
        .width = 128,
        .height = 128,
        .seed = 90,
        .sleep_after_ticks = 30,
    });
    const auto applied =
        completed ? meat2d::net::apply_chunk_delta(target, *completed) : std::nullopt;
    check(applied.has_value(), "reassembled chunk delta did not apply");
    check(applied && applied->changed_cells == meat2d::cells_per_chunk,
          "chunk delta did not update every encoded cell");
    check(target.cell({17, 29}).material == source.cell({17, 29}).material &&
              target.cell({17, 29}).variant == source.cell({17, 29}).variant,
          "chunk cell changed during RLE replication");
    check(applied && applied->chunk_hash == source.chunk_hash(0),
          "chunk delta did not carry the sender's chunk hash");
    check(applied && target.chunk_hash(0) == applied->chunk_hash,
          "applied chunk hash diverged from the encoded chunk hash");
    check(source.chunk_hash(1) != source.chunk_hash(0),
          "distinct chunks unexpectedly share a hash");

    auto corrupted = encoded.value_or(std::vector<std::uint8_t>{});
    if (!corrupted.empty()) {
        corrupted[0] = 0xFFU;
    }
    check(!meat2d::net::apply_chunk_delta(target, corrupted).has_value(),
          "chunk delta with an invalid codec version was accepted");

    const auto corner_interest = meat2d::net::interested_chunks(source, {0, 0}, 1);
    check(corner_interest.size() == 4, "corner interest was not clamped to world chunks");
    const auto exact_interest = meat2d::net::interested_chunks(source, {100, 100}, 0);
    check(exact_interest.size() == 1, "zero-radius interest included extra chunks");
}

} // namespace meat2d_tests

