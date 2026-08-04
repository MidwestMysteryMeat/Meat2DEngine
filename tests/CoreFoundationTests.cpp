#include "meat2d/ai/LivingSimulation.hpp"
#include "meat2d/c_api.h"
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

void test_cell_layout_and_protocol() {
    check(sizeof(meat2d::Cell) == 8, "authoritative cell must remain eight bytes");
    check(sizeof(meat2d::life::OrganismCell) == 8,
          "authoritative organism cell must remain eight bytes");
    check(sizeof(meat2d::net::PacketHeader) == 28, "network header layout unexpectedly changed");
    check(meat2d::net::maximum_players == 8, "first multiplayer target must remain eight players");
}

void test_c_api_world_surface() {
    check(meat2d_c_api_version() == MEAT2D_C_API_VERSION,
          "C ABI reported an unexpected version");
    check(meat2d_world_create(0, 32, 1, 30) == nullptr,
          "C ABI accepted an invalid world dimension");
    auto* world = meat2d_world_create(32, 24, 123, 30);
    check(world != nullptr, "C ABI could not create a valid world");
    if (world == nullptr) {
        return;
    }
    std::int32_t width = 0;
    std::int32_t height = 0;
    check(meat2d_world_get_dimensions(world, &width, &height) == MEAT2D_STATUS_OK &&
              width == 32 && height == 24,
          "C ABI returned incorrect dimensions");
    check(meat2d_world_set_material(world, 4, 5, 3) == MEAT2D_STATUS_OK,
          "C ABI could not set a valid material");
    std::uint8_t material = 0;
    check(meat2d_world_get_material(world, 4, 5, &material) == MEAT2D_STATUS_OK && material == 3,
          "C ABI could not read a material it wrote");
    check(meat2d_world_set_material(world, 4, 5, 255) == MEAT2D_STATUS_INVALID_MATERIAL,
          "C ABI accepted an invalid material");
    check(meat2d_world_get_material(world, 32, 0, &material) == MEAT2D_STATUS_OUT_OF_BOUNDS,
          "C ABI accepted an out-of-bounds read");
    meat2d_tick_stats stats{};
    check(meat2d_world_step(world, &stats) == MEAT2D_STATUS_OK && stats.tick == 1,
          "C ABI could not step the world");
    std::vector<std::uint8_t> pixels(32U * 24U * 4U);
    check(meat2d_world_rasterize_rgba(world, pixels.data(), pixels.size()) == MEAT2D_STATUS_OK,
          "C ABI could not rasterize the world");
    meat2d_world_destroy(world);
}

void test_fixed_timestep_accumulator() {
    meat2d::core::FixedTimestep clock(10, 2);
    check(clock.ticks_per_second() == 10U && clock.max_steps_per_advance() == 2U &&
              clock.fixed_interval() == std::chrono::milliseconds(100),
          "fixed timestep configuration was not normalized correctly");
    const auto half_tick = clock.advance(std::chrono::milliseconds(50));
    check(half_tick.steps == 0U && half_tick.interpolation_alpha > 0.49 &&
              half_tick.interpolation_alpha < 0.51 && !half_tick.dropped_time,
          "fixed timestep did not preserve a partial render interval");
    const auto one_tick = clock.advance(std::chrono::milliseconds(50));
    check(one_tick.steps == 1U && one_tick.interpolation_alpha == 0.0 &&
              !one_tick.dropped_time,
          "fixed timestep did not release exactly one simulation tick");
    const auto capped = clock.advance(std::chrono::seconds(2));
    check(capped.steps == 2U && capped.dropped_time && capped.interpolation_alpha == 0.0,
          "fixed timestep did not bound a stalled-frame catch-up");
    clock.reset();
    const auto negative = clock.advance(std::chrono::milliseconds(-1));
    check(negative.steps == 0U && negative.interpolation_alpha == 0.0,
          "fixed timestep accepted negative elapsed time");
}

void test_deterministic_rng() {
    meat2d::DeterministicRng first(0x12345678ULL);
    meat2d::DeterministicRng second(0x12345678ULL);
    check(first.state() == second.state(), "deterministic RNG did not retain its seed");
    for (int index = 0; index < 128; ++index) {
        check(first.next_u64() == second.next_u64(),
              "equivalent deterministic RNG streams diverged");
        const auto first_bounded = first.uniform(7U);
        const auto second_bounded = second.uniform(7U);
        const auto first_zero = first.uniform(0U);
        const auto second_zero = second.uniform(0U);
        check(first_bounded == second_bounded && first_bounded < 7U && first_zero == 0U &&
                  second_zero == 0U,
              "deterministic RNG returned an out-of-range bounded value");
    }
    meat2d::DeterministicRng probabilities(1U);
    check(!probabilities.chance(0U, 10U) && probabilities.chance(10U, 10U) &&
              !probabilities.chance(1U, 0U),
          "deterministic RNG chance bounds were not enforced");
    first.reseed(0U);
    check(first.state() != 0U, "deterministic RNG accepted an all-zero state");
}

void test_scene_stack_transitions() {
    meat2d::scene::SceneStack stack;
    check(stack.register_scene("Main", meat2d::scene::Scene("main")) &&
              stack.register_scene("Pause", meat2d::scene::Scene("pause")) &&
              stack.register_scene("Game", meat2d::scene::Scene("game")) &&
              !stack.register_scene("Main", meat2d::scene::Scene("duplicate")) &&
              stack.active_name() == "Main" && stack.depth() == 1U,
          "scene stack did not register a deterministic initial scene");
    check(stack.push("Pause") && stack.active_name() == "Pause" && stack.depth() == 2U &&
              stack.pop() && stack.active_name() == "Main" && stack.depth() == 1U &&
              stack.replace("Game") && stack.active_name() == "Game" &&
              stack.transitions().size() == 3U,
          "scene stack push/pop/replace transitions were not deterministic");
    check(stack.transitions()[0].type == meat2d::scene::SceneTransitionType::Push &&
              stack.transitions()[1].type == meat2d::scene::SceneTransitionType::Pop &&
              stack.transitions()[2].from == "Main" && stack.transitions()[2].to == "Game",
          "scene stack did not retain transition history in order");
    check(stack.unregister_scene("Pause") && !stack.unregister_scene("Game") &&
              stack.find("Main") != nullptr && stack.active() != nullptr,
          "scene stack allowed removal of an active scene or lost a registered scene");
    stack.clear_transitions();
    check(stack.transitions().empty(), "scene stack could not clear transient transition history");
}

void test_scene_history_undo_redo() {
    meat2d::scene::SceneHistory history(meat2d::scene::Scene("history"), 3U);
    const auto actor = history.scene().create_entity("Actor");
    check(history.checkpoint() && history.undo_count() == 1U && history.redo_count() == 0U,
          "scene history could not record its first edit");
    check(history.scene().add_group(actor, "player") && history.checkpoint() &&
              history.undo_count() == 2U,
          "scene history could not checkpoint a second edit");
    check(history.undo() && history.scene().contains(actor) &&
              !history.scene().has_group(actor, "player") && history.redo_count() == 1U,
          "scene history undo did not restore the prior scene state");
    check(history.redo() && history.scene().has_group(actor, "player") &&
              history.undo_count() == 2U,
          "scene history redo did not restore the forward scene state");
    check(history.scene().add_group(actor, "selected") && history.checkpoint() &&
              history.redo_count() == 0U,
          "scene history did not discard a stale redo branch after editing");
    history.clear_history();
    check(history.undo_count() == 0U && history.redo_count() == 0U,
          "scene history could not establish a new baseline");
}

void test_scene_diffs() {
    meat2d::scene::Scene base("base");
    const auto unchanged = base.create_entity("Unchanged");
    const auto changed = base.create_entity("Changed");
    const auto removed = base.create_entity("Removed");
    (void)base.add_transform(changed, {.position = {1, 2}});

    meat2d::scene::Scene target = base;
    target.find(changed)->name = "Changed Target";
    target.destroy_entity(removed);
    const auto added = target.create_entity("Added");
    check(unchanged != meat2d::scene::invalid_entity && added > removed,
          "scene diff fixture could not establish stable IDs");
    const auto differences = base.diff(target);
    check(differences.size() == 3U && differences[0] == meat2d::scene::SceneDifference{
                                             .type = meat2d::scene::SceneDifferenceType::Changed,
                                             .entity = changed} &&
              differences[1] == meat2d::scene::SceneDifference{
                                     .type = meat2d::scene::SceneDifferenceType::Removed,
                                     .entity = removed} &&
              differences[2] == meat2d::scene::SceneDifference{
                                     .type = meat2d::scene::SceneDifferenceType::Added,
                                     .entity = added},
          "scene diff did not report stable added/removed/changed entities");
    check(target.diff(target).empty(), "scene diff reported changes for an identical scene");

    const auto patch = base.make_patch(target);
    meat2d::scene::Scene patched = base;
    patched.clear_events();
    check(patch.base_hash == base.state_hash() && patch.target_hash == target.state_hash() &&
              patch.operations.size() == differences.size() && patched.apply_patch(patch) &&
              patched.state_hash() == target.state_hash() && patched.diff(target).empty(),
          "scene patch did not reproduce the exact target scene atomically");
    meat2d::scene::Scene wrong_base("wrong-base");
    const auto wrong_base_hash = wrong_base.state_hash();
    check(!wrong_base.apply_patch(patch) && wrong_base.state_hash() == wrong_base_hash,
          "scene patch applied to an unrelated baseline");
    auto tampered_patch = patch;
    tampered_patch.target_hash ^= 1U;
    const auto before_tampered_patch = base.state_hash();
    check(!base.apply_patch(tampered_patch) && base.state_hash() == before_tampered_patch,
          "tampered scene patch partially modified its target");

    meat2d::scene::Scene cycle_base("cycle-base");
    const auto cycle_left = cycle_base.create_entity("Left");
    const auto cycle_right = cycle_base.create_entity("Right");
    meat2d::scene::Entity cycle_left_target{};
    cycle_left_target.id = cycle_left;
    cycle_left_target.name = "Left";
    cycle_left_target.enabled = true;
    cycle_left_target.parent = cycle_right;
    meat2d::scene::Entity cycle_right_target{};
    cycle_right_target.id = cycle_right;
    cycle_right_target.name = "Right";
    cycle_right_target.enabled = true;
    cycle_right_target.parent = cycle_left;
    meat2d::scene::ScenePatch cycle_patch{};
    cycle_patch.scene_name = cycle_base.name();
    cycle_patch.next_entity_id = 3U;
    cycle_patch.base_hash = cycle_base.state_hash();
    cycle_patch.operations = {
        {.type = meat2d::scene::SceneDifferenceType::Changed, .entity = cycle_left_target},
        {.type = meat2d::scene::SceneDifferenceType::Changed, .entity = cycle_right_target},
    };
    const auto before_cycle_patch = cycle_base.state_hash();
    check(!cycle_base.apply_patch(cycle_patch) && cycle_base.state_hash() == before_cycle_patch &&
              cycle_base.parent_of(cycle_left) == meat2d::scene::invalid_entity &&
              cycle_base.parent_of(cycle_right) == meat2d::scene::invalid_entity,
          "invalid cyclic scene patch was not rejected atomically");
}

void test_scene_snapshots() {
    meat2d::scene::Scene source("snapshot");
    const auto actor = source.create_entity("Actor");
    (void)source.add_transform(actor, {.position = {12, 24}});
    const auto snapshot = meat2d::scene::capture_snapshot(source);
    check(snapshot && snapshot->state_hash == source.state_hash() && !snapshot->bytes.empty(),
          "scene snapshot capture did not preserve a bounded document and hash");
    if (!snapshot) {
        return;
    }
    const auto decoded = meat2d::scene::decode_snapshot(*snapshot);
    check(decoded && decoded->state_hash() == source.state_hash(),
          "scene snapshot could not decode its validated scene");

    auto tampered_hash = *snapshot;
    ++tampered_hash.state_hash;
    check(!meat2d::scene::decode_snapshot(tampered_hash),
          "scene snapshot accepted a mismatched integrity hash");
    auto tampered_bytes = *snapshot;
    tampered_bytes.bytes.back() ^= 0x01U;
    check(!meat2d::scene::decode_snapshot(tampered_bytes),
          "scene snapshot accepted tampered serialized bytes");

    meat2d::scene::Scene target("target");
    const auto before_restore = target.state_hash();
    check(!meat2d::scene::restore_snapshot(target, tampered_hash) &&
              target.state_hash() == before_restore &&
              meat2d::scene::restore_snapshot(target, *snapshot) &&
              target.state_hash() == source.state_hash(),
          "scene snapshot restore was not atomic on validation failure");
    check(!meat2d::scene::capture_snapshot(source, snapshot->bytes.size() - 1U).has_value() &&
              !meat2d::scene::capture_snapshot(source,
                                                meat2d::scene::maximum_scene_snapshot_bytes + 1U)
                   .has_value(),
          "scene snapshot size bounds were not enforced");
}

void test_scene_entity_components_and_hashing() {
    using meat2d::scene::Collider;
    using meat2d::scene::ColliderShape;
    using meat2d::scene::Scene;
    using meat2d::scene::Sprite;
    using meat2d::scene::Transform;

    Scene first("cavern");
    const auto player = first.create_entity("Player");
    const auto door = first.create_entity("Door");
    check(player != meat2d::scene::invalid_entity && door != meat2d::scene::invalid_entity,
          "scene did not allocate entity IDs");
    check(first.entities().size() == 2U && first.contains(player),
          "scene did not retain created entities");
    check(first.add_transform(player, Transform{.position = {12, 24}, .scale = {1, 1}}) != nullptr,
          "scene rejected a valid transform component");
    check(first.add_sprite(player,
                           Sprite{
                               .asset_id = 7,
                               .source = {0, 0, 16, 16},
                               .layer = 2,
                           }) != nullptr,
          "scene rejected a valid sprite component");
    check(first.add_collider(door,
                             Collider{
                                 .shape = ColliderShape::Box,
                                 .bounds = {32, 16, 16, 32},
                                 .sensor = true,
                                 .category_bits = 2,
                                 .mask_bits = 3,
                             }) != nullptr,
          "scene rejected a valid collider component");
    check(first.add_rigid_body(player,
                               {
                                   .velocity = {1, 2},
                                   .acceleration = {0, 1},
                                   .max_velocity = {8, 8},
                               }) != nullptr,
          "scene rejected a valid rigid-body component");
    check(first.add_transform(999U) == nullptr, "scene accepted a component for an unknown entity");

    Scene second("cavern");
    const auto second_player = second.create_entity("Player");
    const auto second_door = second.create_entity("Door");
    check(second.add_transform(second_player,
                               Transform{.position = {12, 24}, .scale = {1, 1}}) != nullptr,
          "equivalent scene rejected its transform component");
    check(second.add_sprite(second_player,
                            Sprite{
                                .asset_id = 7,
                                .source = {0, 0, 16, 16},
                                .layer = 2,
                            }) != nullptr,
          "equivalent scene rejected its sprite component");
    check(second.add_collider(second_door,
                              Collider{
                                  .shape = ColliderShape::Box,
                                  .bounds = {32, 16, 16, 32},
                                  .sensor = true,
                                  .category_bits = 2,
                                  .mask_bits = 3,
                              }) != nullptr,
          "equivalent scene rejected its collider component");
    check(second.add_rigid_body(second_player,
                                {
                                    .velocity = {1, 2},
                                    .acceleration = {0, 1},
                                    .max_velocity = {8, 8},
                                }) != nullptr,
          "equivalent scene rejected its rigid-body component");
    check(first.state_hash() == second.state_hash(),
          "equivalent scenes produced different deterministic hashes");

    const auto encoded = first.serialize();
    const auto decoded = meat2d::scene::Scene::deserialize(encoded);
    check(decoded.has_value() && decoded->name() == "cavern" &&
              decoded->state_hash() == first.state_hash(),
          "scene did not survive a serialize/deserialize round trip");
    auto truncated = encoded;
    truncated.pop_back();
    check(!meat2d::scene::Scene::deserialize(truncated).has_value(),
          "truncated scene data was accepted");
    auto bad_magic = encoded;
    bad_magic[0] = 'X';
    check(!meat2d::scene::Scene::deserialize(bad_magic).has_value(),
          "scene data with an invalid magic was accepted");

    check(first.remove_sprite(player), "scene could not remove an existing component");
    check(first.state_hash() != second.state_hash(),
          "scene hash did not change after component mutation");
    check(first.destroy_entity(player) && !first.contains(player),
          "scene did not destroy an existing entity");
    const auto replacement = first.create_entity("Replacement");
    check(replacement > player, "scene reused a destroyed entity ID");
    check(!first.destroy_entity(player) && !first.remove_collider(player),
          "scene reported success for a missing entity");

    first.clear();
    check(first.entities().empty() && first.create_entity("Fresh") == 1U,
          "scene clear did not reset its lifecycle state");
}

void test_scene_collision_queries() {
    meat2d::scene::Scene scene("collision");
    const auto solid = scene.create_entity("Solid");
    const auto sensor = scene.create_entity("Sensor");
    check(scene.add_transform(solid, {.position = {10, 20}}) != nullptr,
          "scene rejected the solid transform");
    check(scene.add_collider(solid, {.bounds = {0, 0, 8, 8}}) != nullptr,
          "scene rejected the solid collider");
    check(scene.add_transform(sensor, {.position = {40, 20}}) != nullptr,
          "scene rejected the sensor transform");
    check(scene.add_collider(sensor,
                             {
                                 .bounds = {0, 0, 8, 8},
                                 .sensor = true,
                             }) != nullptr,
          "scene rejected the sensor collider");

    const auto solid_bounds = scene.world_collider_bounds(solid);
    check(solid_bounds && solid_bounds->x == 10 && solid_bounds->y == 20,
          "scene did not transform local collider bounds into world space");
    const auto all_hits = scene.query_colliders({5, 15, 20, 20});
    check(all_hits.size() == 1U && all_hits.front() == solid,
          "scene collider query returned an unexpected solid hit");
    const auto sensor_hits = scene.query_colliders({35, 15, 20, 20});
    check(sensor_hits.size() == 1U && sensor_hits.front() == sensor,
          "scene collider query did not return sensor hits when requested");
    check(scene.query_colliders({35, 15, 20, 20}, false).empty(),
          "scene collider query did not filter sensor hits");
    check(!scene.world_collider_bounds(999U).has_value(),
          "scene returned collider bounds for an unknown entity");
}

void test_scene_hierarchy_and_tags() {
    meat2d::scene::Scene scene("hierarchy");
    const auto root = scene.create_entity("Root");
    const auto child = scene.create_entity("Child");
    const auto grandchild = scene.create_entity("Grandchild");
    check(scene.add_transform(root, {.position = {100, 50}}) != nullptr &&
              scene.add_transform(child, {.position = {12, 8}}) != nullptr &&
              scene.add_transform(grandchild, {.position = {3, 4}}) != nullptr,
          "scene rejected hierarchy transforms");
    check(scene.set_parent(child, root) && scene.set_parent(grandchild, child),
          "scene rejected a valid parent hierarchy");
    check(scene.world_position(grandchild) == meat2d::Vec2i{115, 62},
          "scene did not compose local positions through its parent hierarchy");
    check(!scene.set_parent(root, grandchild) && scene.set_parent(child, root),
          "scene accepted a cyclic parent relationship");
    check(scene.add_tag(child, "actors") && scene.add_tag(grandchild, "actors") &&
              scene.add_tag(grandchild, "selectable") && !scene.add_tag(child, "actors"),
          "scene tag operations did not enforce unique tags");
    check(scene.add_group(root, "actors") && scene.has_group(root, "actors") &&
              scene.find_group("actors").size() == 3U && scene.remove_group(root, "actors") &&
              !scene.has_group(root, "actors"),
          "scene group aliases did not preserve deterministic tag behavior");
    const auto tagged = scene.find_tagged("actors");
    check(tagged.size() == 2U && tagged[0] == child && tagged[1] == grandchild,
          "scene tag query did not preserve deterministic entity order");
    check(scene.remove_tag(grandchild, "selectable") && !scene.has_tag(grandchild, "selectable"),
          "scene could not remove a tag");
    const auto encoded = scene.serialize();
    const auto decoded = meat2d::scene::Scene::deserialize(encoded);
    check(decoded && decoded->state_hash() == scene.state_hash() &&
              decoded->world_position(grandchild) == meat2d::Vec2i{115, 62} &&
              decoded->has_tag(child, "actors"),
          "scene hierarchy and tags did not survive serialization");

    check(scene.add_sprite(child, {.layer = 3}) != nullptr &&
              scene.add_sprite(grandchild, {.layer = 3, .visible = false}) != nullptr &&
              scene.find_sprites_in_layer(3).size() == 1U &&
              scene.find_sprites_in_layer(3, false).size() == 2U,
          "scene render-layer query did not filter sprites deterministically");

    scene.clear_events();
    const auto copied_root = scene.duplicate_subtree(root, meat2d::scene::invalid_entity,
                                                      "Copied Root");
    check(copied_root.has_value() && scene.entities().size() == 6U,
          "scene could not duplicate a complete entity subtree");
    if (copied_root) {
        check(scene.find(*copied_root)->name == "Copied Root" &&
                  scene.world_position(*copied_root) == meat2d::Vec2i{100, 50},
              "duplicated subtree did not preserve root data");
    }
    check(scene.events().size() == 5U &&
              scene.events().front().type == meat2d::scene::SceneEventType::EntityCreated &&
              scene.events().back().type == meat2d::scene::SceneEventType::ParentChanged,
          "subtree duplication did not emit deterministic lifecycle events");

    meat2d::scene::Scene prefab("player-prefab");
    const auto prefab_root = prefab.create_entity("Player");
    const auto prefab_weapon = prefab.create_entity("Weapon");
    check(prefab.add_transform(prefab_root, {.position = {7, 9}, .scale = {2, 2}}) != nullptr &&
              prefab.add_sprite(prefab_root, {.asset_id = 42, .layer = 5}) != nullptr &&
              prefab.add_tag(prefab_root, "player") && prefab.add_collider(prefab_weapon) != nullptr &&
              prefab.set_parent(prefab_weapon, prefab_root),
          "prefab source scene could not be prepared");
    prefab.find(prefab_root)->enabled = false;
    meat2d::scene::Scene destination("level");
    const auto spawn = destination.create_entity("Spawn");
    destination.clear_events();
    const auto instance = destination.instantiate_subtree(prefab, prefab_root, spawn, "Hero");
    check(instance.has_value() && destination.entities().size() == 3U,
          "scene could not instantiate a cross-scene prefab subtree");
    if (instance) {
        const auto* copied = destination.find(*instance);
        const auto copied_weapon_it = std::find_if(
            destination.entities().begin(), destination.entities().end(),
            [instance](const auto& entity) { return entity.parent == *instance; });
        const auto* copied_weapon = copied_weapon_it == destination.entities().end()
                                        ? nullptr
                                        : &*copied_weapon_it;
        check(copied != nullptr && copied_weapon != nullptr && copied->name == "Hero" &&
                  !copied->enabled && copied->transform == prefab.find(prefab_root)->transform &&
                  copied->sprite == prefab.find(prefab_root)->sprite && copied->tags ==
                      prefab.find(prefab_root)->tags && destination.parent_of(*instance) == spawn &&
                  destination.parent_of(copied_weapon->id) == *instance &&
                  destination.world_position(*instance) == meat2d::Vec2i{7, 9},
              "prefab instance did not preserve components, hierarchy, or local transform");
    }
    check(destination.instantiate_subtree(prefab, 999U).has_value() == false &&
              destination.instantiate_subtree(prefab, prefab_root, 999U).has_value() == false,
          "prefab instantiation accepted invalid source or destination entities");
    check(!scene.instantiate_subtree(scene, root, grandchild).has_value(),
          "same-scene prefab instantiation accepted a cyclic destination parent");
    meat2d::scene::SceneOverride override_data{};
    override_data.entity = instance.value_or(meat2d::scene::invalid_entity);
    override_data.name = "Champion";
    override_data.enabled = true;
    override_data.parent = meat2d::scene::invalid_entity;
    override_data.tags = std::vector<std::string>{"hero", "selectable"};
    override_data.transform = meat2d::scene::Transform{{20, 30}, {1, 1}};
    override_data.sprite = std::optional<meat2d::scene::Sprite>{};
    destination.clear_events();
    check(instance && destination.apply_override(override_data),
          "scene rejected a valid editor-managed prefab override");
    if (instance) {
        const auto* overridden = destination.find(*instance);
        check(overridden != nullptr && overridden->name == "Champion" && overridden->enabled &&
                  overridden->parent == meat2d::scene::invalid_entity &&
                  overridden->tags == std::vector<std::string>{"hero", "selectable"} &&
                  overridden->transform == meat2d::scene::Transform{{20, 30}, {1, 1}} &&
                  !overridden->sprite,
              "editor-managed override did not update or clear requested fields");
    }
    const auto before_invalid_override = destination.state_hash();
    meat2d::scene::SceneOverride invalid_override{};
    invalid_override.entity = 999U;
    invalid_override.tags = std::vector<std::string>{"duplicate", "duplicate"};
    check(!destination.apply_override(invalid_override) &&
              destination.state_hash() == before_invalid_override,
          "invalid editor override partially modified the scene");
    meat2d::scene::Scene cycle_scene("override-cycle");
    const auto cycle_a = cycle_scene.create_entity("A");
    const auto cycle_b = cycle_scene.create_entity("B");
    std::vector<meat2d::scene::SceneOverride> cycle_overrides(2U);
    cycle_overrides[0].entity = cycle_a;
    cycle_overrides[0].parent = cycle_b;
    cycle_overrides[1].entity = cycle_b;
    cycle_overrides[1].parent = cycle_a;
    const auto before_cycle_override = cycle_scene.state_hash();
    check(!cycle_scene.apply_overrides(cycle_overrides) &&
              cycle_scene.state_hash() == before_cycle_override &&
              cycle_scene.parent_of(cycle_a) == meat2d::scene::invalid_entity &&
              cycle_scene.parent_of(cycle_b) == meat2d::scene::invalid_entity,
          "override batch accepted a cycle or partially applied it");
    scene.clear_events();
    check(scene.add_sprite(root) != nullptr && scene.remove_sprite(root) &&
              scene.add_tag(root, "zeta") && scene.remove_tag(root, "zeta") &&
              scene.events().size() == 4U,
          "scene component and tag mutations did not emit events");
    scene.clear_events();
    check(copied_root && scene.destroy_entity(*copied_root) && scene.entities().size() == 3U &&
              scene.events().size() == 3U &&
              scene.events().front().type == meat2d::scene::SceneEventType::EntityDestroyed &&
              scene.events().back().entity == *copied_root,
          "destroying a scene parent did not remove its subtree safely");
}

} // namespace meat2d_tests
