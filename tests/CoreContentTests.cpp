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

void test_tile_map_content_and_serialization() {
    meat2d::assets::TileMap map(8, 4, 2);
    check(map.width() == 8 && map.height() == 4 && map.layer_count() == 2,
          "tile map did not create its requested dimensions and layers");
    check(map.define_tile({.id = 1, .atlas_source = {0, 0, 16, 16}, .solid = true}) &&
              map.define_tile({.id = 2, .atlas_source = {16, 0, 16, 16}, .solid = false}),
          "tile map rejected valid tile definitions");
    check(map.add_layer("Actors", 10, true), "tile map rejected a valid extra layer");
    check(map.layer_count() == 3U && map.layer_info(2) != nullptr &&
              map.layer_info(2)->name == "Actors",
          "tile map lost its extra layer metadata");
    check(map.set_tile(0, {2, 1}, 1) && map.set_tile(0, {3, 1}, 2) &&
              map.set_tile(1, {4, 2}, 1) && map.set_tile(2, {1, 1}, 2),
          "tile map rejected valid tile writes");
    check(!map.set_tile(0, {0, 0}, 99) && map.tile(0, {0, 0}) == meat2d::assets::empty_tile,
          "tile map accepted an undefined tile ID");
    const auto collisions = map.solid_cells(0, {1, 0, 4, 3});
    check(collisions.size() == 1U && collisions.front() == meat2d::RectI{2, 1, 1, 1},
          "tile map solid-cell query returned incorrect collision metadata");
    const auto encoded = map.serialize();
    const auto decoded = meat2d::assets::TileMap::deserialize(encoded);
    check(decoded.has_value(), "tile map serialized data could not be decoded");
    if (decoded) {
        check(decoded->state_hash() == map.state_hash(),
              "tile map state changed during serialization round trip");
        check(decoded->tile(1, {4, 2}) == 1,
              "tile map lost a layered tile during serialization round trip");
    }
    auto truncated = encoded;
    truncated.pop_back();
    check(!meat2d::assets::TileMap::deserialize(truncated).has_value(),
          "truncated tile map data was accepted");
    auto bad_magic = encoded;
    bad_magic[0] = 'X';
    check(!meat2d::assets::TileMap::deserialize(bad_magic).has_value(),
          "tile map data with invalid magic was accepted");
}

void test_kinematic_scene_motion() {
    meat2d::scene::Scene scene("movement");
    const auto floor = scene.create_entity("Floor");
    const auto wall = scene.create_entity("Wall");
    const auto player = scene.create_entity("Player");
    check(scene.add_transform(floor, {.position = {0, 20}}) != nullptr &&
              scene.add_collider(floor, {.bounds = {0, 0, 100, 10}}) != nullptr &&
              scene.add_transform(wall, {.position = {40, 0}}) != nullptr &&
              scene.add_collider(wall, {.bounds = {0, 0, 10, 100}}) != nullptr &&
              scene.add_transform(player, {.position = {10, 10}}) != nullptr &&
              scene.add_collider(player, {.bounds = {0, 0, 8, 8}}) != nullptr,
          "kinematic scene setup failed");

    const auto falling = meat2d::scene::move_and_collide(scene, player, {0, 10});
    check(falling.hit_vertical && falling.grounded && falling.applied.y == 2 &&
              scene.find(player)->transform->position.y == 12,
          "kinematic movement did not stop and ground an actor on a floor");
    const auto walking = meat2d::scene::move_and_collide(scene, player, {10, 0});
    check(!walking.hit_horizontal && walking.applied.x == 10 &&
              scene.find(player)->transform->position.x == 20,
          "kinematic movement rejected a clear horizontal step");
    const auto blocked = meat2d::scene::move_and_collide(scene, player, {15, 0});
    check(blocked.hit_horizontal && blocked.applied.x == 12 &&
              scene.find(player)->transform->position.x == 32,
          "kinematic movement passed through or stopped inside a solid wall");
}

void test_rigid_body_step_and_particles() {
    meat2d::scene::Scene scene("rigid");
    const auto floor = scene.create_entity("Floor");
    const auto player = scene.create_entity("Player");
    check(scene.add_transform(floor, {.position = {0, 20}}) != nullptr &&
              scene.add_collider(floor, {.bounds = {0, 0, 100, 10}}) != nullptr &&
              scene.add_transform(player, {.position = {10, 10}}) != nullptr &&
              scene.add_collider(player, {.bounds = {0, 0, 8, 8}}) != nullptr &&
              scene.add_rigid_body(player, {.max_velocity = {8, 8}}) != nullptr,
          "rigid-body scene setup failed");
    const auto first_step = meat2d::scene::step_rigid_bodies(scene);
    check(first_step.bodies == 1U && first_step.collisions == 0U &&
              scene.find(player)->rigid_body->velocity.y == 1,
          "rigid-body step did not apply deterministic gravity");
    const auto second_step = meat2d::scene::step_rigid_bodies(scene);
    check(second_step.bodies == 1U && second_step.collisions == 1U && second_step.grounded == 1U &&
              scene.find(player)->rigid_body->velocity.y == 0,
          "rigid-body step did not stop and ground the actor");

    meat2d::render::ParticleSystem first_particles(2);
    meat2d::render::ParticleSystem second_particles(2);
    const meat2d::render::ParticleConfig config{
        .position = {2, 3},
        .velocity = {1, 0},
        .acceleration = {0, 1},
        .lifetime_ticks = 5,
        .size = 2,
        .color = {255, 128, 64, 255},
    };
    check(first_particles.spawn(config) != meat2d::render::invalid_particle &&
              second_particles.spawn(config) != meat2d::render::invalid_particle,
          "particle system rejected a valid particle");
    check(first_particles.spawn(config) != meat2d::render::invalid_particle &&
              first_particles.spawn(config) == meat2d::render::invalid_particle,
          "particle system exceeded its configured capacity");
    check(second_particles.spawn(config) != meat2d::render::invalid_particle,
          "equivalent particle system rejected a valid particle");
    first_particles.step(2);
    second_particles.step(2);
    check(first_particles.state_hash() == second_particles.state_hash() &&
              first_particles.particles().front().position == meat2d::Vec2i{4, 6},
          "particle simulation was not deterministic");
    first_particles.step(3);
    check(first_particles.particles().empty(), "expired particles were not retired");
}

void test_collision_layers_and_debug_draw() {
    meat2d::scene::Scene scene("layers");
    const auto wall = scene.create_entity("Wall");
    const auto actor = scene.create_entity("Actor");
    check(scene.add_transform(wall, {.position = {20, 0}}) != nullptr &&
              scene.add_collider(wall,
                                 {
                                     .bounds = {0, 0, 8, 40},
                                     .category_bits = 2,
                                     .mask_bits = 1,
                                 }) != nullptr &&
              scene.add_transform(actor, {.position = {10, 10}}) != nullptr &&
              scene.add_collider(actor,
                                 {
                                     .bounds = {0, 0, 8, 8},
                                     .category_bits = 1,
                                     .mask_bits = 1,
                                 }) != nullptr,
          "collision-layer scene setup failed");
    const auto ignored = meat2d::scene::move_and_collide(scene, actor, {10, 0});
    check(!ignored.hit_horizontal && ignored.applied.x == 10,
          "collision masks did not ignore an unrelated category");
    scene.find(actor)->transform->position.x = 10;
    scene.find(actor)->collider->mask_bits = 2;
    const auto blocked = meat2d::scene::move_and_collide(scene, actor, {10, 0});
    check(blocked.hit_horizontal && blocked.applied.x == 2,
          "collision masks did not enable the requested category");

    meat2d::render::DebugDrawList debug(3);
    check(debug.add_line({0, 0}, {4, 4}, {255, 0, 0, 255}) &&
              debug.add_rectangle({1, 2, 3, 4}) && debug.add_circle({5, 6}, 2) &&
              !debug.add_text({0, 0}, "overflow"),
          "debug draw list did not enforce its primitive bound");
    check(debug.primitives().size() == 3U &&
              debug.primitives()[0].type == meat2d::render::DebugPrimitiveType::Line,
          "debug draw list stored the wrong primitive data");
    debug.clear();
    check(debug.primitives().empty(), "debug draw list did not clear its commands");
    check(!debug.add_circle({0, 0}, -1), "debug draw list accepted a negative radius");
}

void test_sprite_batch() {
    meat2d::scene::Scene scene("sprites");
    const auto back = scene.create_entity("Back");
    const auto front = scene.create_entity("Front");
    const auto hidden = scene.create_entity("Hidden");
    const auto offscreen = scene.create_entity("Offscreen");
    check(scene.add_transform(back, {.position = {10, 10}, .scale = {2, 1}}) != nullptr &&
              scene.add_sprite(back, {.asset_id = 2, .source = {0, 0, 16, 8}, .layer = 1}) !=
                  nullptr &&
              scene.add_transform(front, {.position = {12, 12}, .scale = {-1, -1}}) != nullptr &&
              scene.add_sprite(front, {.asset_id = 3, .source = {16, 0, 8, 8}, .layer = 3}) !=
                  nullptr &&
              scene.add_sprite(hidden, {.asset_id = 4, .source = {0, 0, 8, 8}, .visible = false}) !=
                  nullptr &&
              scene.add_transform(offscreen, {.position = {500, 500}}) != nullptr &&
              scene.add_sprite(offscreen, {.asset_id = 5, .source = {0, 0, 8, 8}}) != nullptr,
          "sprite batch fixture could not be prepared");
    meat2d::render::Camera2D camera;
    camera.set_center({50, 30});
    camera.set_viewport({100, 60});
    meat2d::render::SpriteBatch batch(3);
    check(batch.build(scene, camera) && batch.commands().size() == 2U &&
              batch.commands()[0].entity == back && batch.commands()[1].entity == front &&
              batch.commands()[0].destination == meat2d::RectI{10, 10, 32, 8} &&
              batch.commands()[1].flip_x && batch.commands()[1].flip_y,
          "sprite batch did not cull or order camera-visible sprites deterministically");
    check(batch.build(scene, camera, false) && batch.commands().size() == 3U,
          "sprite batch could not include explicitly hidden sprites");
    meat2d::render::SpriteBatch bounded(1);
    check(!bounded.build(scene, camera, false) && bounded.commands().empty(),
          "sprite batch exceeded its command budget or partially replaced commands");
}

void test_static_mesh_instance_batch() {
    const std::vector<meat2d::render::StaticMeshInstance> instances{
        {.entity = 2, .mesh = 20, .position = {10, 10}, .local_bounds = {0, 0, 16, 8}, .layer = 1},
        {.entity = 1, .mesh = 10, .position = {20, 10}, .local_bounds = {0, 0, 8, 8}, .layer = 1},
        {.entity = 3, .mesh = 30, .position = {10, 10}, .local_bounds = {0, 0, 8, 8},
         .layer = 2, .visible = false},
        {.entity = 4, .mesh = 40, .position = {500, 500}, .local_bounds = {0, 0, 8, 8}},
    };
    meat2d::render::Camera2D camera;
    camera.set_center({50, 30});
    camera.set_viewport({100, 60});
    meat2d::render::StaticMeshInstanceBatch batch(3);
    check(batch.build(instances, camera) && batch.commands().size() == 2U &&
              batch.commands()[0].mesh == 10U && batch.commands()[1].mesh == 20U &&
              batch.commands()[0].world_bounds == meat2d::RectI{20, 10, 8, 8} &&
              batch.commands()[1].screen_bounds == meat2d::RectI{10, 10, 16, 8},
          "static mesh instances were not culled or sorted for instanced rendering");
    meat2d::render::StaticMeshInstanceBatch bounded(1);
    check(!bounded.build(instances, camera) && bounded.commands().empty(),
          "static mesh instance batch exceeded its command budget or partially replaced commands");
}

void test_scene_editor_model() {
    meat2d::scene::Scene initial("editor");
    const auto parent = initial.create_entity("Parent");
    const auto back = initial.create_entity("Back");
    const auto front = initial.create_entity("Front");
    check(initial.add_transform(parent, {.position = {10, 10}}) != nullptr &&
              initial.add_transform(back, {.position = {0, 0}}) != nullptr &&
              initial.add_sprite(back, {.asset_id = 1, .source = {0, 0, 16, 16}, .layer = 1}) !=
                  nullptr &&
              initial.add_transform(front, {.position = {0, 0}}) != nullptr &&
              initial.add_sprite(front, {.asset_id = 2, .source = {0, 0, 16, 16}, .layer = 2}) !=
                  nullptr &&
              initial.set_parent(back, parent) && initial.set_parent(front, parent),
          "scene editor fixture could not be prepared");
    meat2d::tools::SceneEditor editor(initial, 4);
    editor.camera().set_center({18, 18});
    editor.camera().set_viewport({40, 40});
    check(editor.children_of(parent).size() == 2U && editor.select_at({12, 12}) &&
              editor.selected() == front,
          "scene editor did not select the deterministic topmost viewport entity");
    meat2d::scene::SceneOverride rename{};
    rename.entity = front;
    rename.name = "Selected Front";
    check(editor.apply_override(rename) && editor.scene().find(front)->name == "Selected Front" &&
              editor.history().undo_count() == 1U && editor.undo() &&
              editor.scene().find(front)->name == "Front" && editor.redo() &&
              editor.scene().find(front)->name == "Selected Front" && editor.selected() == front,
          "scene editor override history did not undo and redo selection edits");
    check(!editor.select(999U), "scene editor selected an unknown entity");
}

void test_neural_network_and_learning_agents() {
    const auto scale = meat2d::ai::neural_fixed_scale;
    meat2d::ai::FixedNeuralNetwork network;
    check(network.set_layers({
              {.input_units = 2,
               .output_units = 3,
               .weights = {scale, 0, 0, scale, -scale, -scale},
               .biases = {0, 0, 0},
               .activation = meat2d::ai::NeuralActivation::ReLU},
          }),
          "fixed neural network rejected a valid bounded model");
    const std::vector<std::int32_t> observation{2 * scale, scale};
    const auto output = network.infer(observation);
    check(output && output->size() == 3U && (*output)[0] == 2 * scale &&
              (*output)[1] == scale && (*output)[2] == 0,
          "fixed neural network inference was not deterministic fixed-point output");
    const auto network_hash = network.state_hash();
    meat2d::ai::MachineLearningAgent agent(7U, 1U);
    check(agent.set_policy(std::move(network), 3U), "ML agent rejected a valid policy");
    agent.begin_tick();
    check(agent.decide(observation) == 0U && !agent.decide(observation).has_value() &&
              agent.decisions_this_tick() == 1U,
          "ML agent did not enforce deterministic action selection and decision budget");
    agent.record_reward(5);
    check(agent.total_reward() == 5 && agent.state_hash() != network_hash,
          "ML agent did not retain bounded reward state");
    check(!agent.set_policy(meat2d::ai::FixedNeuralNetwork{}, 0U),
          "ML agent accepted an invalid empty policy");
}

void test_deterministic_crowds() {
    meat2d::ai::CrowdSimulation first(4U);
    meat2d::ai::CrowdSimulation second(4U);
    const meat2d::ai::CrowdAgent agent_two{
        .id = 2, .position = {1, 1}, .target = {8, 1}, .max_step = 1,
        .separation_radius = 2, .enabled = true};
    const meat2d::ai::CrowdAgent agent_one{
        .id = 1, .position = {0, 1}, .target = {8, 1}, .max_step = 1,
        .separation_radius = 2, .enabled = true};
    meat2d::ai::CrowdAgent duplicate{};
    duplicate.id = 1;
    check(first.add_agent(agent_two) && first.add_agent(agent_one) &&
              second.add_agent(agent_one) && second.add_agent(agent_two) &&
              !first.add_agent(duplicate),
          "crowd simulation did not enforce stable IDs and bounded capacity");
    const auto first_step = first.step({0, 0, 4, 4});
    const auto second_step = second.step({0, 0, 4, 4});
    check(first_step.moved > 0U && first.state_hash() == second.state_hash() &&
              first.agents().front().id == 1U && first.agents().back().id == 2U &&
              first.find(1U)->position.x <= 3 && first.find(2U)->position.x <= 3,
          "crowd agents were not deterministic, sorted, or bounds-safe");
    check(first_step.moved == second_step.moved && first.set_target(1U, {0, 0}) &&
              first.remove_agent(2U) && !first.remove_agent(2U),
          "crowd target and removal operations were not deterministic");
}

void test_bounded_learning_environment() {
    const auto scale = meat2d::ai::neural_fixed_scale;
    meat2d::ai::FixedNeuralNetwork policy;
    check(policy.set_layers({
              {.input_units = 2,
               .output_units = 2,
               .weights = {scale, 0, 0, scale},
               .biases = {0, 0},
               .activation = meat2d::ai::NeuralActivation::ReLU},
          }),
          "learning environment test could not build a bounded policy");
    meat2d::ai::MachineLearningAgent agent(9U, 1U);
    check(agent.set_policy(std::move(policy), 2U),
          "learning environment test could not install its policy");

    meat2d::ai::LearningEnvironment environment({.observation_units = 2,
                                                  .maximum_episode_steps = 2U});
    check(environment.valid() && environment.begin_episode() && environment.active(),
          "learning environment rejected a valid episode");
    const std::vector<std::int32_t> first_observation{scale, 0};
    const std::vector<std::int32_t> invalid_observation{scale};
    check(environment.choose_action(agent, first_observation) == 0U &&
              !environment.choose_action(agent, invalid_observation).has_value() &&
              environment.finish_step(3, false) && environment.steps() == 1U &&
              environment.transitions().size() == 1U &&
              environment.transitions()[0].reward == 3 &&
              environment.transitions()[0].action == 0U,
          "learning environment did not validate and record its first transition");
    const std::vector<std::int32_t> second_observation{0, scale};
    check(environment.choose_action(agent, second_observation) == 1U &&
              environment.finish_step(-1, true) && !environment.active() &&
              environment.steps() == 2U && environment.transitions().back().terminal &&
              environment.state_hash() != 0U,
          "learning environment did not terminate and hash a bounded episode");
    check(!meat2d::ai::LearningEnvironment({.observation_units = 0,
                                             .maximum_episode_steps = 2U})
                .valid(),
          "learning environment accepted an invalid observation contract");
}

void test_mcp_gateway_safety_and_discovery() {
    meat2d::scene::Scene initial("MCP Test Scene");
    const auto actor = initial.create_entity("Actor");
    meat2d::tools::SceneEditor editor(std::move(initial), 4U);
    meat2d::tools::McpGateway gateway(editor, "capability-token");

    check(!gateway.authenticate("wrong") && gateway.authenticate("capability-token") &&
              meat2d::tools::McpGateway::constant_time_equal("same", "same") &&
              !meat2d::tools::McpGateway::constant_time_equal("same", "other"),
          "MCP gateway did not enforce capability-token authentication");
    check(!gateway.search("scene", "wrong").success &&
              gateway.search("scene", "capability-token").tools.size() == 1U &&
              gateway.describe("scene", "capability-token").tools.front().actions.size() == 6U,
          "MCP gateway discovery did not use authenticated bounded descriptors");
    check(gateway.execute("scene", "inspect", {}, "capability-token").success &&
              gateway.execute("scene", "list_entities", {}, "capability-token").payload.find(
                  "Actor") != std::string::npos,
          "MCP gateway could not perform authenticated read-only scene inspection");
    check(!gateway.execute("scene", "select", "entity=1", "capability-token").success &&
              gateway.execute("scene", "select", "entity=" + std::to_string(actor),
                              "capability-token", "write")
                      .success &&
              editor.selected() == actor,
          "MCP gateway allowed unsafe mutation without explicit write consent");
    check(!gateway.execute("scene", "select", "entity=bad", "capability-token", "write").success &&
              gateway.configure("scene", false, "capability-token", "write").success &&
              !gateway.execute("scene", "inspect", {}, "capability-token").success &&
              gateway.configure("scene", true, "capability-token", "write").success,
          "MCP gateway did not enforce bounded parameters and tool configuration");

    meat2d::tools::McpGateway read_only(
        editor, "read-token", static_cast<std::uint8_t>(meat2d::tools::McpCapability::ReadScene));
    const auto inspected = read_only.execute("scene", "inspect", {}, "read-token", {}, 42U);
    check(inspected.success && inspected.request_id == 42U &&
              !read_only.execute("scene", "select", "entity=1", "read-token", "write", 43U)
                   .success &&
              read_only.execute("scene", "inspect", {}, "read-token", {}, 42U).code ==
                  "invalid_request" &&
              read_only.audit_log().size() == 3U,
          "MCP gateway did not enforce scoped capabilities or correlated request IDs");

    read_only.reset_session();
    for (std::uint64_t request = 1U;
         request <= meat2d::tools::McpGateway::maximum_requests_per_session; ++request) {
        check(read_only.search({}, "read-token", request).success,
              "MCP gateway rejected a request inside the session budget");
    }
    check(read_only.search({}, "read-token", 129U).code == "rate_limited" &&
              read_only.requests_this_session() ==
                  meat2d::tools::McpGateway::maximum_requests_per_session,
          "MCP gateway did not enforce its bounded session request budget");
}

void test_input_state_and_action_map() {
    meat2d::input::InputState input;
    input.set_key(meat2d::input::Key::D, true);
    input.set_mouse_button(meat2d::input::MouseButton::Left, true);
    input.set_gamepad_button(0, meat2d::input::GamepadButton::South, true);
    input.set_gamepad_axis(0, meat2d::input::GamepadAxis::LeftX, 1234);
    input.set_touch(0, 42U, true, 100, 200);
    input.set_touch(0, 42U, true, 120, 210);
    input.set_mouse_position(10, 12);
    input.set_mouse_position(16, 15);
    check(input.key_down(meat2d::input::Key::D) && input.key_pressed(meat2d::input::Key::D),
          "input state did not record a key press");
    check(input.mouse_down(meat2d::input::MouseButton::Left) &&
              input.mouse_pressed(meat2d::input::MouseButton::Left),
          "input state did not record a mouse press");
    check(input.gamepad_button_down(0, meat2d::input::GamepadButton::South) &&
              input.gamepad_button_pressed(0, meat2d::input::GamepadButton::South) &&
              input.gamepad_axis(0, meat2d::input::GamepadAxis::LeftX) == 1234,
          "input state did not record gamepad buttons and axes");
    check(input.touch_down(0) && input.touch_pressed(0) && input.touch_id(0) == 42U &&
              input.touch_x(0) == 120 && input.touch_y(0) == 210 && input.touch_delta_x(0) == 120 &&
              input.touch_delta_y(0) == 210,
          "input state did not record bounded touch contact data");
    check(input.mouse_x() == 16 && input.mouse_y() == 15 && input.mouse_delta_x() == 16 &&
              input.mouse_delta_y() == 15,
          "input state did not accumulate mouse movement");

    meat2d::input::ActionMap actions;
    const auto move = actions.register_action("move_right");
    check(move != meat2d::input::invalid_action && actions.find_action("move_right") == move,
          "action map did not register an action");
    check(actions.bind_key(move, meat2d::input::Key::D) &&
              actions.bind_mouse_button(move, meat2d::input::MouseButton::Left) &&
              actions.bind_gamepad_button(move, 0, meat2d::input::GamepadButton::South) &&
              actions.bind_touch(move, 0),
          "action map rejected valid bindings");
    check(actions.down(move, input) && actions.pressed(move, input),
          "action map did not resolve active bindings");
    input.begin_frame();
    check(actions.down(move, input) && !actions.pressed(move, input) &&
              input.mouse_delta_x() == 0 && input.mouse_delta_y() == 0,
          "input frame reset cleared held state or retained edge state");
    input.set_key(meat2d::input::Key::D, false);
    input.set_mouse_button(meat2d::input::MouseButton::Left, false);
    input.set_gamepad_button(0, meat2d::input::GamepadButton::South, false);
    input.set_touch(0, 42U, false, 120, 210);
    check(actions.released(move, input), "action map did not resolve released bindings");
    check(actions.clear_bindings(move) && !actions.down(move, input),
          "action map did not clear bindings");
}

void test_camera_transforms_and_clamping() {
    meat2d::render::Camera2D camera;
    camera.set_viewport({100, 50});
    camera.set_center({50, 25});
    check(camera.visible_rect() == meat2d::RectI{0, 0, 100, 50},
          "camera visible rectangle did not match its viewport");
    check(camera.world_to_screen({50, 25}) == meat2d::Vec2i{50, 25} &&
              camera.screen_to_world({50, 25}) == meat2d::Vec2i{50, 25},
          "camera center transform was not reversible");
    camera.set_zoom_percent(200);
    check(camera.visible_rect().width == 50 && camera.visible_rect().height == 25,
          "camera zoom did not scale its visible rectangle");
    camera.set_center({0, 0});
    camera.clamp_to({0, 0, 200, 100});
    const auto clamped = camera.visible_rect();
    check(clamped.x >= 0 && clamped.y >= 0 && clamped.x + clamped.width <= 200 &&
              clamped.y + clamped.height <= 100,
          "camera clamp allowed the viewport outside world bounds");
}

void test_animation_playback_and_camera_source() {
    const meat2d::assets::SpriteSheet sheet{
        .image = "assets/player.png",
        .frame_width = 16,
        .frame_height = 16,
        .animations =
            {
                {
                    .name = "run",
                    .first_frame = 1,
                    .frame_count = 3,
                    .frames_per_second = 10,
                    .loop = true,
                },
                {
                    .name = "attack",
                    .first_frame = 4,
                    .frame_count = 2,
                    .frames_per_second = 30,
                    .loop = false,
                },
            },
    };
    meat2d::assets::SpriteAnimator animator;
    animator.set_sheet(&sheet);
    check(animator.play("run") && animator.animation_name() == "run" &&
              animator.frame_index() == 1U,
          "sprite animator did not start the requested animation");
    animator.advance(6);
    check(animator.frame_index() == 2U, "sprite animator advanced at the wrong fixed-tick rate");
    animator.advance(6);
    check(animator.frame_index() == 3U, "sprite animator did not advance to the next frame");
    animator.advance(6);
    check(animator.frame_index() == 1U && !animator.finished(),
          "looping sprite animation did not wrap");
    check(animator.play("attack") && animator.frame_index() == 4U,
          "sprite animator could not switch animations");
    animator.advance(4);
    check(animator.frame_index() == 5U && animator.finished(),
          "non-looping sprite animation did not stop on its final frame");
    const auto frame = animator.frame(96, 16);
    check(frame && frame->index == 5U, "sprite animator returned an invalid frame rectangle");

    meat2d::World world({.width = 64, .height = 48});
    meat2d::render::WorldView view;
    view.update(world);
    meat2d::render::Camera2D camera;
    camera.set_viewport({20, 12});
    camera.set_center({32, 24});
    check(view.camera_source(camera) == meat2d::RectI{22, 18, 20, 12},
          "world view did not expose the camera source rectangle");
}

} // namespace meat2d_tests
