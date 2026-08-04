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
    const auto decoded = meat2d::life::decode_traits(meat2d::life::encode_traits(expected));
    check(decoded.photosynthesis == expected.photosynthesis, "photosynthesis gene changed");
    check(decoded.digestion == expected.digestion, "digestion gene changed");
    check(decoded.motility == expected.motility, "motility gene changed");
    check(decoded.reproduction == expected.reproduction, "reproduction gene changed");
    check(decoded.heat_preference == expected.heat_preference, "heat-preference gene changed");
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
    const bool seeded =
        simulation.organisms().seed({12, 12}, meat2d::life::decomposer_genome, 1'500);
    check(seeded, "cellular organism failed to seed");
    const auto stats = simulation.step();
    check(stats.organisms.consumed_cells == 1, "decomposer did not consume plant matter");
    check(simulation.world().material({12, 12}) == meat2d::MaterialId::Empty,
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

void test_material_catalog() {
    for (std::size_t index = 0; index < meat2d::material_count; ++index) {
        const auto material = static_cast<meat2d::MaterialId>(index);
        const auto& definition = meat2d::material_definition(material);
        check(meat2d::is_valid(material), "catalog contains an invalid material ID");
        check(!definition.name.empty(), "catalog contains an unnamed material");
        check(definition.color.a != 0U, "catalog material is fully transparent");
    }
    check(!meat2d::is_valid(meat2d::MaterialId::Count),
          "Count sentinel must not be a usable material");
    check(meat2d::has_flag(meat2d::MaterialId::Metal, meat2d::MaterialFlags::Conductive),
          "metal lost its conductive property");
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

    check(world.material({32, 59}) == meat2d::MaterialId::Sand,
          "sand did not settle immediately above stone");
    check(world.material({32, 60}) == meat2d::MaterialId::Stone,
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
    check(world.material({11, 11}) == meat2d::MaterialId::Steam,
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
    check(world.material({7, 8}) == meat2d::MaterialId::Obsidian,
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
        check(world.material({8, 7}) == meat2d::MaterialId::Empty,
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
        check(world.material({8, 7}) == meat2d::MaterialId::Empty,
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
        check(world.material({10, 10}) == meat2d::MaterialId::Fire,
              "hot gunpowder did not explode");
        check(world.material({11, 10}) == meat2d::MaterialId::Fire,
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
    check(simulation.state_hash() == reversed.state_hash(),
          "command enqueue order changed authoritative state");
    const auto stats = simulation.step();
    reversed.step();
    check(stats.applied_commands == 2, "queued world commands were not applied");
    check(simulation.world().material({4, 5}) == meat2d::MaterialId::Concrete,
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
        const auto grazer = simulation.spawn_agent(meat2d::ai::AgentKind::Grazer, {5, 9});
        simulation.step();
        check(grazer != 0, "grazer failed to spawn");
        check(simulation.world().material({6, 9}) == meat2d::MaterialId::Empty,
              "grazer did not consume adjacent plant life");
        check(simulation.find_agent(grazer) != nullptr &&
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
        const auto grazer = simulation.spawn_agent(meat2d::ai::AgentKind::Grazer, {6, 9});
        simulation.step();
        check(simulation.find_agent(grazer) != nullptr &&
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
        const auto worker = simulation.spawn_agent(meat2d::ai::AgentKind::Worker, {5, 9});
        simulation.step();
        check(simulation.find_agent(worker) != nullptr &&
                  simulation.find_agent(worker)->carried == meat2d::MaterialId::Debris,
              "worker did not collect adjacent debris");
        check(simulation.world().material({6, 9}) == meat2d::MaterialId::Empty,
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
        check(simulation.world().material({4, 9}) == meat2d::MaterialId::Debris,
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
        check(first.state_hash() == second.state_hash(), "equal living simulations diverged");
        if (failures != 0) {
            return;
        }
    }
}

} // namespace meat2d_tests
