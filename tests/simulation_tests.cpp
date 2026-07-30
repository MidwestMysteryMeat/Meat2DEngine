#include "meat2d/ai/LivingSimulation.hpp"
#include "meat2d/net/ChunkCodec.hpp"
#include "meat2d/net/Fragmentation.hpp"
#include "meat2d/net/PacketCodec.hpp"
#include "meat2d/net/Protocol.hpp"
#include "meat2d/net/Reliability.hpp"
#include "meat2d/net/Session.hpp"
#include "meat2d/net/UdpSocket.hpp"
#include "meat2d/sim/Scenario.hpp"
#include "meat2d/sim/World.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void test_cell_layout_and_protocol() {
    check(sizeof(meat2d::Cell) == 8, "authoritative cell must remain eight bytes");
    check(
        sizeof(meat2d::life::OrganismCell) == 8,
        "authoritative organism cell must remain eight bytes");
    check(
        sizeof(meat2d::net::PacketHeader) == 28,
        "network header layout unexpectedly changed");
    check(
        meat2d::net::maximum_players == 8,
        "first multiplayer target must remain eight players");
}

void test_material_catalog() {
    for (std::size_t index = 0; index < meat2d::material_count; ++index) {
        const auto material = static_cast<meat2d::MaterialId>(index);
        const auto& definition = meat2d::material_definition(material);
        check(meat2d::is_valid(material), "catalog contains an invalid material ID");
        check(!definition.name.empty(), "catalog contains an unnamed material");
        check(definition.color.a != 0U, "catalog material is fully transparent");
    }
    check(
        !meat2d::is_valid(meat2d::MaterialId::Count),
        "Count sentinel must not be a usable material");
    check(
        meat2d::has_flag(meat2d::MaterialId::Metal, meat2d::MaterialFlags::Conductive),
        "metal lost its conductive property");
}

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
    check(
        datagram->size() <= meat2d::net::maximum_datagram_bytes,
        "encoded packet exceeded the safe datagram budget");

    const auto packet = meat2d::net::decode_packet(*datagram);
    check(packet.has_value(), "encoded packet did not decode");
    check(
        packet && packet->header.sequence == header.sequence,
        "packet sequence changed during serialization");
    check(
        packet && packet->header.acknowledgement_bits == header.acknowledgement_bits,
        "packet acknowledgement window changed during serialization");
    const auto decoded_hello =
        packet ? meat2d::net::decode_hello(packet->payload) : std::nullopt;
    check(decoded_hello.has_value(), "hello message did not decode");
    check(
        decoded_hello && decoded_hello->player_name == hello.player_name,
        "player name changed during serialization");
    check(
        decoded_hello && decoded_hello->client_nonce == hello.client_nonce,
        "client nonce changed during serialization");

    auto truncated = *datagram;
    truncated.pop_back();
    check(
        !meat2d::net::decode_packet(truncated).has_value(),
        "truncated datagram was accepted");
    auto bad_magic = *datagram;
    bad_magic[0] ^= 0xFFU;
    check(
        !meat2d::net::decode_packet(bad_magic).has_value(),
        "datagram with invalid protocol magic was accepted");
    auto bad_flags = *datagram;
    bad_flags[7] = 0x80U;
    check(
        !meat2d::net::decode_packet(bad_flags).has_value(),
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
    const auto decoded_input =
        meat2d::net::decode_input(meat2d::net::encode_input(input));
    check(decoded_input.has_value(), "input message did not decode");
    check(
        decoded_input && decoded_input->target == input.target &&
            decoded_input->material == input.material &&
            decoded_input->session_token == input.session_token,
        "input message changed during serialization");

    const auto oversized_welcome = meat2d::net::decode_welcome(
        meat2d::net::encode_welcome({
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
    check(
        !oversized_welcome.has_value(),
        "welcome message could request an unsafe client allocation");
}

void test_reliable_sequence_window() {
    meat2d::net::AcknowledgementTracker tracker;
    check(tracker.observe(100), "first sequence was rejected");
    check(tracker.observe(102), "newer sequence was rejected");
    check(tracker.observe(101), "out-of-order sequence inside the window was rejected");
    check(!tracker.observe(101), "duplicate sequence was accepted");
    check(tracker.acknowledgement() == 102, "latest acknowledgement is incorrect");
    check(
        meat2d::net::sequence_acknowledged(
            100,
            tracker.acknowledgement(),
            tracker.acknowledgement_bits()),
        "acknowledgement bits lost an older sequence");
    check(
        meat2d::net::sequence_acknowledged(
            101,
            tracker.acknowledgement(),
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
    const auto initial = sender.make_packet(
        meat2d::net::PacketType::Welcome,
        payload,
        0,
        0,
        true);
    check(initial.header.sequence == 1, "reliable sequence did not start at one");
    check(sender.pending_packets() == 1, "reliable packet was not retained");

    auto retransmissions = sender.collect_retransmissions(2, 2);
    check(retransmissions.size() == 1, "lost packet was not retransmitted");
    check(receiver.receive(retransmissions.front().header), "first retransmission was rejected");

    retransmissions = sender.collect_retransmissions(4, 4);
    check(retransmissions.size() == 1, "unacknowledged packet stopped retransmitting");
    check(
        !receiver.receive(retransmissions.front().header),
        "duplicate retransmission was accepted twice");
    const auto acknowledgement = receiver.make_acknowledgement(4, 4);
    sender.receive(acknowledgement.header);
    check(sender.pending_packets() == 0, "acknowledged packet remained pending");
    check(
        sender.stats().retransmissions == 2,
        "retransmission statistics are incorrect");
    check(
        receiver.stats().duplicates_received == 1,
        "duplicate receive statistics are incorrect");
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
            source.set_material(
                {x, y},
                ((x + y) & 1) == 0 ? meat2d::MaterialId::Stone
                                   : meat2d::MaterialId::Wood);
        }
    }

    const auto encoded = meat2d::net::encode_chunk_delta(source, 0);
    check(encoded.has_value(), "valid chunk delta did not encode");
    check(
        encoded && encoded->size() > meat2d::net::maximum_fragment_data_bytes,
        "large chunk delta did not exercise fragmentation");

    const auto fragments =
        encoded ? meat2d::net::fragment_payload(77, *encoded)
                : std::vector<std::vector<std::uint8_t>>{};
    check(fragments.size() > 1, "chunk delta was not split into MTU-safe fragments");
    for (const auto& fragment : fragments) {
        check(
            fragment.size() + meat2d::net::encoded_header_bytes <=
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
    check(
        applied && applied->changed_cells == meat2d::cells_per_chunk,
        "chunk delta did not update every encoded cell");
    check(
        target.cell({17, 29}).material == source.cell({17, 29}).material &&
            target.cell({17, 29}).variant == source.cell({17, 29}).variant,
        "chunk cell changed during RLE replication");

    auto corrupted = encoded.value_or(std::vector<std::uint8_t>{});
    if (!corrupted.empty()) {
        corrupted[0] = 0xFFU;
    }
    check(
        !meat2d::net::apply_chunk_delta(target, corrupted).has_value(),
        "chunk delta with an invalid codec version was accepted");

    const auto corner_interest = meat2d::net::interested_chunks(source, {0, 0}, 1);
    check(corner_interest.size() == 4, "corner interest was not clamped to world chunks");
    const auto exact_interest =
        meat2d::net::interested_chunks(source, {100, 100}, 0);
    check(exact_interest.size() == 1, "zero-radius interest included extra chunks");
}

std::optional<meat2d::net::Datagram> wait_for_datagram(
    meat2d::net::UdpSocket& socket) {
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
    check(
        received && received->bytes == std::vector<std::uint8_t>(outbound.begin(), outbound.end()),
        "UDP loopback payload was corrupted");
    if (!received) {
        return;
    }

    const std::array<std::uint8_t, 3> reply{42, 43, 44};
    check(server.send(received->sender, reply), "UDP loopback reply failed");
    const auto returned = wait_for_datagram(client);
    check(returned.has_value(), "UDP loopback client received no reply");
    check(
        returned && returned->bytes == std::vector<std::uint8_t>(reply.begin(), reply.end()),
        "UDP loopback reply was corrupted");
}

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
    });
    check(server.start(), "authoritative server failed to start");
    if (!server.running()) {
        return;
    }

    meat2d::net::AuthoritativeClient client;
    check(
        client.connect(
            {
                .address = "localhost",
                .port = server.port(),
            },
            "Session Test",
            0xC0FFEEU),
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

    check(
        client.paint({10, 10}, meat2d::MaterialId::Stone, 2),
        "connected client could not send a paint input");
    bool server_applied = false;
    bool client_replicated = false;
    for (int update = 0; update < 120; ++update) {
        client.update();
        server.update();
        client.update();
        server_applied =
            server.simulation().world().material({10, 10}) ==
            meat2d::MaterialId::Stone;
        const auto* mirror = client.replicated_world();
        client_replicated =
            mirror != nullptr &&
            mirror->material({10, 10}) == meat2d::MaterialId::Stone;
        if (server_applied && client_replicated && client.latest_snapshot()) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(server_applied, "server did not apply validated client input");
    check(client_replicated, "interest-managed chunk delta did not reach client mirror");
    check(client.latest_snapshot().has_value(), "client received no authoritative snapshot");

    for (int update = 0; update < 140; ++update) {
        client.update();
        server.update();
        client.update();
    }
    check(
        server.client_count() == 1,
        "active client timed out despite periodic keepalives");

    client.disconnect();
    for (int update = 0; update < 10 && server.client_count() != 0; ++update) {
        server.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(server.client_count() == 0, "server retained a disconnected client slot");
}

void test_organism_genome_and_ecology() {
    const meat2d::life::OrganismTraits expected{
        .photosynthesis = 12,
        .digestion = 13,
        .motility = 9,
        .reproduction = 8,
        .heat_preference = 4,
        .resilience = 11,
        .mutation = 7,
        .pigment = 5,
    };
    const auto decoded =
        meat2d::life::decode_traits(meat2d::life::encode_traits(expected));
    check(decoded.photosynthesis == expected.photosynthesis, "photosynthesis gene changed");
    check(decoded.digestion == expected.digestion, "digestion gene changed");
    check(decoded.motility == expected.motility, "motility gene changed");
    check(decoded.reproduction == expected.reproduction, "reproduction gene changed");
    check(
        decoded.heat_preference == expected.heat_preference,
        "heat-preference gene changed");
    check(decoded.resilience == expected.resilience, "resilience gene changed");
    check(decoded.mutation == expected.mutation, "mutation gene changed");
    check(decoded.pigment == expected.pigment, "pigment gene changed");

    meat2d::ai::LivingSimulation simulation({
        .width = 32,
        .height = 24,
        .seed = 87,
        .sleep_after_ticks = 30,
    });
    simulation.world().set_material({12, 12}, meat2d::MaterialId::Plant);
    const bool seeded = simulation.organisms().seed(
        {12, 12},
        meat2d::life::decomposer_genome,
        1'500);
    check(seeded, "cellular organism failed to seed");
    const auto stats = simulation.step();
    check(stats.organisms.consumed_cells == 1, "decomposer did not consume plant matter");
    check(
        simulation.world().material({12, 12}) == meat2d::MaterialId::Empty,
        "consumed plant matter remained in the material field");
}

void test_organism_determinism_and_reproduction() {
    meat2d::ai::LivingSimulation first({
        .width = 48,
        .height = 32,
        .seed = 88,
        .sleep_after_ticks = 30,
    });
    meat2d::ai::LivingSimulation second({
        .width = 48,
        .height = 32,
        .seed = 88,
        .sleep_after_ticks = 30,
    });
    first.organisms().seed({24, 16}, meat2d::life::photosynthetic_genome, 1'400);
    second.organisms().seed({24, 16}, meat2d::life::photosynthetic_genome, 1'400);

    for (int tick = 0; tick < 180; ++tick) {
        first.step();
        second.step();
        check(first.state_hash() == second.state_hash(), "organism fields diverged");
        if (failures != 0) {
            return;
        }
    }
    check(first.organisms().population() > 1U, "organisms did not reproduce");
}

void test_sand_falls_and_stone_stays() {
    meat2d::World world({
        .width = 64,
        .height = 64,
        .seed = 11,
        .sleep_after_ticks = 30,
    });
    for (int x = 0; x < world.width(); ++x) {
        world.set_material({x, 60}, meat2d::MaterialId::Stone);
    }
    world.set_material({32, 2}, meat2d::MaterialId::Sand);

    for (int tick = 0; tick < 80; ++tick) {
        world.step();
    }

    check(
        world.material({32, 59}) == meat2d::MaterialId::Sand,
        "sand did not settle immediately above stone");
    check(
        world.material({32, 60}) == meat2d::MaterialId::Stone,
        "stone moved during cellular simulation");
}

void test_water_conserves_cells() {
    meat2d::World world({
        .width = 96,
        .height = 64,
        .seed = 22,
        .sleep_after_ticks = 30,
    });
    for (int x = 0; x < world.width(); ++x) {
        world.set_material({x, 62}, meat2d::MaterialId::Stone);
    }
    const auto painted = world.paint_disc({48, 8}, 5, meat2d::MaterialId::Water);
    for (int tick = 0; tick < 100; ++tick) {
        world.step();
    }

    std::size_t water_cells = 0;
    int lowest_water = 0;
    for (int y = 0; y < world.height(); ++y) {
        for (int x = 0; x < world.width(); ++x) {
            if (world.material({x, y}) == meat2d::MaterialId::Water) {
                ++water_cells;
                lowest_water = std::max(lowest_water, y);
            }
        }
    }
    check(water_cells == painted, "water cell count changed while flowing");
    check(lowest_water == 61, "water did not reach the floor");
}

void test_temperature_phase_changes() {
    meat2d::World world({
        .width = 16,
        .height = 16,
        .seed = 77,
        .sleep_after_ticks = 30,
    });

    meat2d::Cell frozen_water{};
    frozen_water.material = meat2d::MaterialId::Water;
    frozen_water.temperature = static_cast<std::int16_t>(-10 * 16);
    world.set_cell({4, 4}, frozen_water);

    meat2d::Cell boiling_water{};
    boiling_water.material = meat2d::MaterialId::Water;
    boiling_water.temperature = static_cast<std::int16_t>(120 * 16);
    world.set_cell({11, 11}, boiling_water);

    const auto stats = world.step();
    check(world.material({4, 4}) == meat2d::MaterialId::Ice, "cold water did not freeze");
    check(
        world.material({11, 11}) == meat2d::MaterialId::Steam,
        "boiling water did not become steam");
    check(stats.reacted_cells == 2, "phase-change reaction count is incorrect");
}

void test_lava_water_reaction() {
    meat2d::World world({
        .width = 16,
        .height = 16,
        .seed = 78,
        .sleep_after_ticks = 30,
    });
    world.set_material({7, 8}, meat2d::MaterialId::Lava);
    world.set_material({8, 8}, meat2d::MaterialId::Water);

    world.step();
    check(
        world.material({7, 8}) == meat2d::MaterialId::Obsidian,
        "lava did not cool into obsidian beside water");
    std::size_t steam_cells = 0;
    for (int y = 0; y < world.height(); ++y) {
        for (int x = 0; x < world.width(); ++x) {
            if (world.material({x, y}) == meat2d::MaterialId::Steam) {
                ++steam_cells;
            }
        }
    }
    check(steam_cells == 1, "water did not flash into steam beside lava");
}

void test_chemical_and_electrical_reactions() {
    {
        meat2d::World world({
            .width = 16,
            .height = 16,
            .seed = 79,
            .sleep_after_ticks = 30,
        });
        world.set_material({7, 7}, meat2d::MaterialId::Acid);
        world.set_material({8, 7}, meat2d::MaterialId::Stone);
        world.step();
        check(
            world.material({8, 7}) == meat2d::MaterialId::Empty,
            "acid did not corrode adjacent stone");
    }

    {
        meat2d::World world({
            .width = 16,
            .height = 16,
            .seed = 80,
            .sleep_after_ticks = 30,
        });
        world.set_material({7, 7}, meat2d::MaterialId::Metal);
        world.set_material({8, 7}, meat2d::MaterialId::Electricity);
        world.step();
        check(
            world.material({8, 7}) == meat2d::MaterialId::Empty,
            "electricity cell was not consumed");
        check(world.cell({7, 7}).state > 0U, "electricity did not charge adjacent metal");
    }

    {
        meat2d::World world({
            .width = 20,
            .height = 20,
            .seed = 81,
            .sleep_after_ticks = 30,
        });
        meat2d::Cell hot_powder{};
        hot_powder.material = meat2d::MaterialId::Gunpowder;
        hot_powder.temperature = static_cast<std::int16_t>(300 * 16);
        world.set_cell({10, 10}, hot_powder);
        world.set_material({11, 10}, meat2d::MaterialId::Wood);
        const auto stats = world.step();
        check(
            world.material({10, 10}) == meat2d::MaterialId::Fire,
            "hot gunpowder did not explode");
        check(
            world.material({11, 10}) == meat2d::MaterialId::Fire,
            "explosion did not ignite nearby wood");
        check(stats.reacted_cells >= 2, "explosion did not report its reactions");
    }
}

void add_floor(meat2d::ai::LivingSimulation& simulation, int y) {
    for (int x = 0; x < simulation.world().width(); ++x) {
        simulation.world().set_material({x, y}, meat2d::MaterialId::Stone);
    }
}

void test_tick_ordered_entity_commands() {
    meat2d::ai::LivingSimulation simulation({
        .width = 16,
        .height = 16,
        .seed = 82,
        .sleep_after_ticks = 30,
    });
    meat2d::ai::LivingSimulation reversed({
        .width = 16,
        .height = 16,
        .seed = 82,
        .sleep_after_ticks = 30,
    });
    const bool queued = simulation.queue_command({
        .target_tick = 1,
        .issuer = meat2d::ai::world_issuer,
        .sequence = 7,
        .type = meat2d::ai::CommandType::Paint,
        .target = {4, 5},
        .material = meat2d::MaterialId::Concrete,
    });
    simulation.queue_command({
        .target_tick = 1,
        .issuer = meat2d::ai::world_issuer,
        .sequence = 9,
        .type = meat2d::ai::CommandType::Paint,
        .target = {7, 5},
        .material = meat2d::MaterialId::Wood,
    });
    reversed.queue_command({
        .target_tick = 1,
        .issuer = meat2d::ai::world_issuer,
        .sequence = 9,
        .type = meat2d::ai::CommandType::Paint,
        .target = {7, 5},
        .material = meat2d::MaterialId::Wood,
    });
    reversed.queue_command({
        .target_tick = 1,
        .issuer = meat2d::ai::world_issuer,
        .sequence = 7,
        .type = meat2d::ai::CommandType::Paint,
        .target = {4, 5},
        .material = meat2d::MaterialId::Concrete,
    });
    check(queued, "valid future world command was rejected");
    check(
        simulation.state_hash() == reversed.state_hash(),
        "command enqueue order changed authoritative state");
    const auto stats = simulation.step();
    reversed.step();
    check(stats.applied_commands == 2, "queued world commands were not applied");
    check(
        simulation.world().material({4, 5}) == meat2d::MaterialId::Concrete,
        "tick-ordered paint command changed the wrong cell");
}

void test_grazer_predator_and_worker_ai() {
    {
        meat2d::ai::LivingSimulation simulation({
            .width = 20,
            .height = 16,
            .seed = 83,
            .sleep_after_ticks = 30,
        });
        add_floor(simulation, 10);
        simulation.world().set_material({6, 9}, meat2d::MaterialId::Plant);
        const auto grazer =
            simulation.spawn_agent(meat2d::ai::AgentKind::Grazer, {5, 9});
        simulation.step();
        check(grazer != 0, "grazer failed to spawn");
        check(
            simulation.world().material({6, 9}) == meat2d::MaterialId::Empty,
            "grazer did not consume adjacent plant life");
        check(
            simulation.find_agent(grazer) != nullptr &&
                simulation.find_agent(grazer)->action == meat2d::ai::AgentAction::Eat,
            "grazer did not report its eat action");
    }

    {
        meat2d::ai::LivingSimulation simulation({
            .width = 20,
            .height = 16,
            .seed = 84,
            .sleep_after_ticks = 30,
        });
        add_floor(simulation, 10);
        simulation.spawn_agent(meat2d::ai::AgentKind::Predator, {5, 9});
        const auto grazer =
            simulation.spawn_agent(meat2d::ai::AgentKind::Grazer, {6, 9});
        simulation.step();
        check(
            simulation.find_agent(grazer) != nullptr &&
                simulation.find_agent(grazer)->health == 75U,
            "predator did not damage adjacent prey");
    }

    {
        meat2d::ai::LivingSimulation simulation({
            .width = 20,
            .height = 16,
            .seed = 85,
            .sleep_after_ticks = 30,
        });
        add_floor(simulation, 10);
        simulation.world().set_material({6, 9}, meat2d::MaterialId::Debris);
        const auto worker =
            simulation.spawn_agent(meat2d::ai::AgentKind::Worker, {5, 9});
        simulation.step();
        check(
            simulation.find_agent(worker) != nullptr &&
                simulation.find_agent(worker)->carried == meat2d::MaterialId::Debris,
            "worker did not collect adjacent debris");
        check(
            simulation.world().material({6, 9}) == meat2d::MaterialId::Empty,
            "worker did not remove collected debris");

        simulation.queue_command({
            .target_tick = 2,
            .issuer = worker,
            .sequence = 1,
            .type = meat2d::ai::CommandType::Place,
            .target = {4, 9},
            .material = meat2d::MaterialId::Debris,
        });
        simulation.step();
        check(
            simulation.world().material({4, 9}) == meat2d::MaterialId::Debris,
            "worker did not place its carried material");
    }
}

void test_living_simulation_determinism() {
    meat2d::ai::LivingSimulation first({
        .width = 96,
        .height = 64,
        .seed = 86,
        .sleep_after_ticks = 30,
    });
    meat2d::ai::LivingSimulation second({
        .width = 96,
        .height = 64,
        .seed = 86,
        .sleep_after_ticks = 30,
    });
    add_floor(first, 58);
    add_floor(second, 58);
    for (int x = 12; x < 36; x += 4) {
        first.world().set_material({x, 57}, meat2d::MaterialId::Plant);
        second.world().set_material({x, 57}, meat2d::MaterialId::Plant);
    }
    for (int x = 55; x < 70; x += 3) {
        first.world().set_material({x, 57}, meat2d::MaterialId::Debris);
        second.world().set_material({x, 57}, meat2d::MaterialId::Debris);
    }

    for (const auto position : {meat2d::Vec2i{8, 57}, meat2d::Vec2i{20, 57}}) {
        first.spawn_agent(meat2d::ai::AgentKind::Grazer, position);
        second.spawn_agent(meat2d::ai::AgentKind::Grazer, position);
    }
    first.spawn_agent(meat2d::ai::AgentKind::Predator, {42, 57});
    second.spawn_agent(meat2d::ai::AgentKind::Predator, {42, 57});
    first.spawn_agent(meat2d::ai::AgentKind::Worker, {75, 57});
    second.spawn_agent(meat2d::ai::AgentKind::Worker, {75, 57});

    for (int tick = 0; tick < 240; ++tick) {
        first.step();
        second.step();
        check(
            first.state_hash() == second.state_hash(),
            "equal living simulations diverged");
        if (failures != 0) {
            return;
        }
    }
}

void test_cross_chunk_motion() {
    meat2d::World world({
        .width = 128,
        .height = 128,
        .seed = 33,
        .sleep_after_ticks = 30,
    });
    world.set_material({70, 62}, meat2d::MaterialId::Sand);
    for (int tick = 0; tick < 4; ++tick) {
        world.step();
    }
    check(
        world.material({70, 66}) == meat2d::MaterialId::Sand,
        "sand failed to cross a chunk boundary");
}

void test_determinism() {
    meat2d::World first({
        .width = 192,
        .height = 128,
        .seed = 44,
        .sleep_after_ticks = 30,
    });
    meat2d::World second({
        .width = 192,
        .height = 128,
        .seed = 44,
        .sleep_after_ticks = 30,
    });
    meat2d::seed_sand_lab(first);
    meat2d::seed_sand_lab(second);

    for (int tick = 0; tick < 240; ++tick) {
        first.step();
        second.step();
        check(first.state_hash() == second.state_hash(), "equal worlds diverged");
        if (failures != 0) {
            return;
        }
    }
}

void test_chunks_sleep() {
    meat2d::World world({
        .width = 128,
        .height = 128,
        .seed = 55,
        .sleep_after_ticks = 5,
    });
    world.wake_all();
    meat2d::TickStats stats{};
    for (int tick = 0; tick < 6; ++tick) {
        stats = world.step();
    }
    check(stats.active_chunks == 0, "quiet chunks did not enter sleep");
    check(stats.sleeping_chunks == 4, "unexpected sleeping chunk count");
}

void test_raster_output() {
    meat2d::World world({
        .width = 8,
        .height = 8,
        .seed = 66,
        .sleep_after_ticks = 30,
    });
    world.set_material({3, 4}, meat2d::MaterialId::Sand);
    std::vector<std::uint8_t> pixels(8U * 8U * 4U);
    world.rasterize_rgba(pixels);
    const auto offset = (4U * 8U + 3U) * 4U;
    check(pixels[offset] > pixels[offset + 2U], "sand pixel did not use a warm color");
    check(pixels[offset + 3U] == 255, "raster alpha is not opaque");
}

} // namespace

int main() {
    try {
        test_cell_layout_and_protocol();
        test_packet_codec();
        test_reliable_sequence_window();
        test_chunk_delta_fragmentation();
        test_udp_loopback();
        test_authoritative_client_server_session();
        test_organism_genome_and_ecology();
        test_organism_determinism_and_reproduction();
        test_material_catalog();
        test_sand_falls_and_stone_stays();
        test_water_conserves_cells();
        test_temperature_phase_changes();
        test_lava_water_reaction();
        test_chemical_and_electrical_reactions();
        test_tick_ordered_entity_commands();
        test_grazer_predator_and_worker_ai();
        test_living_simulation_determinism();
        test_cross_chunk_motion();
        test_determinism();
        test_chunks_sleep();
        test_raster_output();
    } catch (const std::exception& exception) {
        std::cerr << "UNCAUGHT: " << exception.what() << '\n';
        return 1;
    }

    if (failures == 0) {
        std::cout << "MEAT2D TESTS PASS\n";
        return 0;
    }
    std::cerr << failures << " test assertion(s) failed\n";
    return 1;
}
