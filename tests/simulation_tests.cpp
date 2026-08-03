#include "meat2d/ai/LivingSimulation.hpp"
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
    check(sizeof(meat2d::life::OrganismCell) == 8,
          "authoritative organism cell must remain eight bytes");
    check(sizeof(meat2d::net::PacketHeader) == 28, "network header layout unexpectedly changed");
    check(meat2d::net::maximum_players == 8, "first multiplayer target must remain eight players");
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

void test_input_state_and_action_map() {
    meat2d::input::InputState input;
    input.set_key(meat2d::input::Key::D, true);
    input.set_mouse_button(meat2d::input::MouseButton::Left, true);
    input.set_mouse_position(10, 12);
    input.set_mouse_position(16, 15);
    check(input.key_down(meat2d::input::Key::D) && input.key_pressed(meat2d::input::Key::D),
          "input state did not record a key press");
    check(input.mouse_down(meat2d::input::MouseButton::Left) &&
              input.mouse_pressed(meat2d::input::MouseButton::Left),
          "input state did not record a mouse press");
    check(input.mouse_x() == 16 && input.mouse_y() == 15 && input.mouse_delta_x() == 16 &&
              input.mouse_delta_y() == 15,
          "input state did not accumulate mouse movement");

    meat2d::input::ActionMap actions;
    const auto move = actions.register_action("move_right");
    check(move != meat2d::input::invalid_action && actions.find_action("move_right") == move,
          "action map did not register an action");
    check(actions.bind_key(move, meat2d::input::Key::D) &&
              actions.bind_mouse_button(move, meat2d::input::MouseButton::Left),
          "action map rejected valid bindings");
    check(actions.down(move, input) && actions.pressed(move, input),
          "action map did not resolve active bindings");
    input.begin_frame();
    check(actions.down(move, input) && !actions.pressed(move, input) &&
              input.mouse_delta_x() == 0 && input.mouse_delta_y() == 0,
          "input frame reset cleared held state or retained edge state");
    input.set_key(meat2d::input::Key::D, false);
    input.set_mouse_button(meat2d::input::MouseButton::Left, false);
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

void test_project_browser_safety_and_editing() {
    const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto test_parent = std::filesystem::temp_directory_path() / ("meat2d-browser-" + unique);
    const auto project = test_parent / "project";
    std::error_code error;
    std::filesystem::create_directories(project / "src", error);
    std::filesystem::create_directories(project / "build", error);
    {
        std::ofstream(project / "CMakeLists.txt") << "project(BrowserTest)\n";
        std::ofstream(project / "src" / "main.cpp") << "int main() { return 0; }\n";
        std::ofstream(project / "build" / "generated.cpp") << "generated\n";
        std::ofstream(test_parent / "sample.png", std::ios::binary) << "fake image payload";
    }

    meat2d::tools::ProjectBrowser browser;
    check(browser.open(project), "project browser could not open a valid project");
    const auto generated_hidden =
        std::none_of(browser.entries().begin(), browser.entries().end(),
                     [](const meat2d::tools::ProjectEntry& entry) {
                         return entry.relative_path.generic_string().starts_with("build/");
                     });
    check(generated_hidden, "project browser exposed generated build files by default");
    const auto source_entry =
        std::find_if(browser.entries().begin(), browser.entries().end(),
                     [](const meat2d::tools::ProjectEntry& entry) {
                         return entry.relative_path.generic_string() == "src/main.cpp";
                     });
    check(source_entry != browser.entries().end() &&
              source_entry->last_write_time ==
                  std::filesystem::last_write_time(project / "src" / "main.cpp", error),
          "project browser did not expose the source file modification time");

    auto loaded = browser.load_text("src/main.cpp");
    check(loaded.success, "project browser could not load source text");
    check(loaded.text.find("int main() { return 0; }") != std::string::npos,
          "project browser changed loaded source text");
    const auto saved = browser.save_text("src/main.cpp", "int main() { return 7; }\n");
    check(saved.success, "project browser could not save source text");
    loaded = browser.load_text("src/main.cpp");
    check(loaded.success && loaded.text == "int main() { return 7; }\n",
          "project browser did not persist the edited source");

    const auto created = browser.create_text_file("config/settings.toml", "[game]\nname='test'\n");
    check(created.success, "project browser could not create a config file");
    const auto imported = browser.import_asset(test_parent / "sample.png", "textures");
    check(imported.success, "project browser could not import an asset");
    check(imported.success && imported.path.parent_path().filename() == "textures" &&
              std::filesystem::is_regular_file(imported.path),
          "asset import escaped or missed the requested project folder");

    check(!browser.load_text("../outside.txt").success,
          "project browser allowed parent-directory traversal");
    check(!browser.create_text_file("../escape.cpp", {}).success,
          "project browser created a file outside the project");
    check(!browser.resolve_for_external_open(test_parent / "sample.png").success,
          "external-open resolver accepted an absolute path");

    browser.set_show_generated(true);
    check(browser.refresh(), "project browser could not rescan generated files");
    check(std::any_of(browser.entries().begin(), browser.entries().end(),
                      [](const meat2d::tools::ProjectEntry& entry) {
                          return entry.relative_path.generic_string() == "build/generated.cpp";
                      }),
          "generated-file opt-in did not expose the build tree");

    browser.close();
    std::filesystem::remove_all(test_parent, error);
    check(!error, "project browser test files could not be cleaned up");
}

void test_project_manager_validation_and_templates() {
    const auto templates = meat2d::tools::locate_template_root();
    check(!templates.empty(), "project manager could not locate its templates");
    if (templates.empty()) {
        return;
    }
    meat2d::tools::ProjectManager manager(templates);
    const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto test_parent =
        std::filesystem::temp_directory_path() / ("meat2d-project-manager-" + unique);

    const auto invalid = manager.create_project({
        .name = "Bad \" CMake ${Name}",
        .directory = test_parent / "invalid",
        .project_template = meat2d::tools::ProjectTemplate::SideScroller,
        .engine_git_tag = "main",
    });
    check(!invalid.success && !std::filesystem::exists(test_parent / "invalid"),
          "project manager accepted an injectable project name");

    const auto valid = manager.create_project({
        .name = "Meat & Potatoes (Test)",
        .directory = test_parent / "valid",
        .project_template = meat2d::tools::ProjectTemplate::TopDown,
        .engine_git_tag = "main",
    });
    check(valid.success, "project manager could not create a valid starter");
    check(std::filesystem::is_regular_file(test_parent / "valid" / "src" / "main.cpp") &&
              std::filesystem::is_regular_file(test_parent / "valid" / "CMakePresets.json") &&
              std::filesystem::is_regular_file(test_parent / "valid" / ".github" / "workflows" /
                                               "build.yml"),
          "generated starter omitted code, presets, or publishing workflow");

    const std::array<meat2d::tools::ProjectTemplate, 9> project_templates{
        meat2d::tools::ProjectTemplate::SideScroller,
        meat2d::tools::ProjectTemplate::TopDown,
        meat2d::tools::ProjectTemplate::Metroidvania,
        meat2d::tools::ProjectTemplate::VisualNovel,
        meat2d::tools::ProjectTemplate::Rpg,
        meat2d::tools::ProjectTemplate::DestructibleArtillery,
        meat2d::tools::ProjectTemplate::CellularRoguelite,
        meat2d::tools::ProjectTemplate::FallingSand,
        meat2d::tools::ProjectTemplate::SandboxSurvival,
    };
    for (std::size_t index = 0; index < project_templates.size(); ++index) {
        const auto result = manager.create_project({
            .name = "Template Test " + std::to_string(index),
            .directory = test_parent / ("template-" + std::to_string(index)),
            .project_template = project_templates[index],
            .engine_git_tag = "main",
        });
        check(result.success, "project manager could not create a selectable game template");
        check(std::filesystem::is_regular_file(test_parent / ("template-" + std::to_string(index)) /
                                               "src" / "main.cpp"),
              "selectable game template omitted its source starter");
    }

    const std::array<std::string_view, 4> fixed_tick_templates{
        "side_scroller", "top_down", "metroidvania", "falling_sand"};
    for (const auto template_name : fixed_tick_templates) {
        std::ifstream source(templates / template_name / "src" / "main.cpp");
        const std::string contents{std::istreambuf_iterator<char>(source),
                                   std::istreambuf_iterator<char>()};
        check(contents.find("meat2d/core/FixedTimestep.hpp") != std::string::npos,
              "interactive template does not use the shared fixed timestep");
        check(contents.find("fixed_seconds") == std::string::npos &&
                  contents.find("double accumulator") == std::string::npos,
              "interactive template retained a duplicated floating-point accumulator");
    }

    std::error_code error;
    std::filesystem::remove_all(test_parent, error);
    check(!error, "project manager test files could not be cleaned up");
}

void test_sprite_sheet_metadata() {
    const meat2d::assets::SpriteSheet sheet{
        .image = "assets/sprites/player.png",
        .frame_width = 16,
        .frame_height = 16,
        .margin = 1,
        .spacing = 2,
        .animations =
            {
                {
                    .name = "idle",
                    .first_frame = 0,
                    .frame_count = 2,
                    .frames_per_second = 6,
                    .loop = true,
                },
                {
                    .name = "run",
                    .first_frame = 2,
                    .frame_count = 4,
                    .frames_per_second = 12,
                    .loop = true,
                },
            },
    };
    check(meat2d::assets::valid_sprite_sheet(sheet, 70, 36),
          "valid sprite sheet metadata was rejected");
    check(meat2d::assets::sprite_frame_count(sheet, 70, 36) == 6,
          "sprite grid frame count is incorrect");
    const auto frame = meat2d::assets::sprite_frame(sheet, 70, 36, 5);
    check(frame && frame->x == 37 && frame->y == 19 && frame->width == 16 && frame->height == 16,
          "sprite frame rectangle is incorrect");
    check(!meat2d::assets::sprite_frame(sheet, 70, 36, 6).has_value(),
          "out-of-range sprite frame was returned");

    const auto encoded = meat2d::assets::encode_sprite_sheet_toml(sheet);
    check(!encoded.empty(), "sprite sheet metadata did not encode");
    const auto decoded = meat2d::assets::decode_sprite_sheet_toml(encoded);
    check(decoded.sheet && *decoded.sheet == sheet,
          "sprite sheet metadata changed during TOML round trip");

    const auto unsafe = meat2d::assets::decode_sprite_sheet_toml("version = 1\n"
                                                                 "image = \"../outside.png\"\n"
                                                                 "frame_width = 16\n"
                                                                 "frame_height = 16\n");
    check(!unsafe.sheet.has_value(), "sprite metadata accepted a path outside the project");
    const auto malformed = meat2d::assets::decode_sprite_sheet_toml("version = 1\nunknown = 3\n");
    check(!malformed.sheet.has_value() && malformed.error_line == 2,
          "malformed sprite metadata did not report its source line");
    const auto hash_in_path = meat2d::assets::decode_sprite_sheet_toml(
        "version = 1\n"
        "image = \"assets/player#alternate.png\" # inline comment\n"
        "frame_width = 16\n"
        "frame_height = 16\n");
    check(hash_in_path.sheet && hash_in_path.sheet->image == "assets/player#alternate.png",
          "sprite metadata treated a hash inside a string as a comment");
}

void test_texture_atlas_cache() {
    const meat2d::assets::SpriteSheet sheet{
        .image = "assets/player.png",
        .frame_width = 16,
        .frame_height = 16,
        .margin = 0,
        .spacing = 0,
        .animations = {},
    };
    meat2d::assets::TextureAtlasCache first(2);
    meat2d::assets::TextureAtlasCache second(2);
    check(first.define(7, sheet, 32, 16) && second.define(8, sheet, 32, 16) &&
              first.define(8, sheet, 32, 16) && second.define(7, sheet, 32, 16) &&
              first.state_hash() == second.state_hash(),
          "texture atlas cache hash depended on registration order");
    const auto region = first.resolve(7, 1);
    check(region && region->image == sheet.image && region->source == meat2d::RectI{16, 0, 16, 16},
          "texture atlas cache did not resolve a validated frame rectangle");
    const auto before_invalid = first.state_hash();
    meat2d::assets::SpriteSheet unsafe{};
    unsafe.image = "../unsafe.png";
    check(!first.define(9, unsafe, 32, 16) &&
              first.state_hash() == before_invalid && !first.resolve(999, 0).has_value(),
          "texture atlas cache accepted unsafe or unknown content");
    check(first.remove(8) && !first.remove(8) && first.size() == 1U,
          "texture atlas cache remove semantics were not deterministic");
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
    check(world.material({70, 66}) == meat2d::MaterialId::Sand,
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

void test_dirty_region_rasterization() {
    meat2d::World world({
        .width = 96,
        .height = 96,
        .seed = 77,
        .sleep_after_ticks = 30,
    });
    world.paint_disc({40, 8}, 6, meat2d::MaterialId::Sand);
    world.paint_disc({70, 8}, 5, meat2d::MaterialId::Water);

    const auto pixel_count = 96U * 96U * 4U;
    std::vector<std::uint8_t> full(pixel_count);
    std::vector<std::uint8_t> partial(pixel_count);
    world.rasterize_rgba(full);
    world.rasterize_rgba_region({0, 0, 96, 96}, partial);
    check(full == partial, "full-world region rasterization diverged from rasterize_rgba");

    world.rasterize_rgba(partial);
    world.clear_dirty();
    bool any_dirty = false;
    for (std::int32_t row = 0; row < world.chunk_rows(); ++row) {
        for (std::int32_t column = 0; column < world.chunk_columns(); ++column) {
            any_dirty = any_dirty || !world.chunk_dirty_rect(column, row).empty();
        }
    }
    check(!any_dirty, "clear_dirty left a non-empty chunk dirty rect");

    for (int tick = 0; tick < 24; ++tick) {
        world.step();
    }
    bool found_dirty = false;
    for (std::int32_t row = 0; row < world.chunk_rows(); ++row) {
        for (std::int32_t column = 0; column < world.chunk_columns(); ++column) {
            const auto region = world.chunk_dirty_rect(column, row);
            if (region.empty()) {
                continue;
            }
            found_dirty = true;
            check(region.x >= 0 && region.y >= 0 && region.x + region.width <= 96 &&
                      region.y + region.height <= 96,
                  "chunk dirty rect escaped the world bounds");
            world.rasterize_rgba_region(region, partial);
        }
    }
    check(found_dirty, "falling material produced no dirty regions");
    world.rasterize_rgba(full);
    check(full == partial, "dirty-region refresh did not reproduce the full rasterization");
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

void test_raycast_and_line_of_sight() {
    meat2d::World world({
        .width = 32,
        .height = 32,
        .seed = 77,
        .sleep_after_ticks = 30,
    });

    const auto clear = world.raycast({1, 5}, {30, 5});
    check(!clear.blocked, "raycast over open space reported a block");
    check(clear.position == meat2d::Vec2i{30, 5}, "unblocked raycast did not report the target");
    check(world.line_of_sight({1, 5}, {30, 5}), "line_of_sight disagreed with an unblocked raycast");

    for (int y = 0; y < world.height(); ++y) {
        world.set_material({15, y}, meat2d::MaterialId::Stone);
    }
    const auto blocked = world.raycast({1, 5}, {30, 5});
    check(blocked.blocked, "raycast passed through a solid wall");
    check(blocked.position == meat2d::Vec2i{15, 5}, "raycast did not stop at the wall face");
    check(blocked.material == meat2d::MaterialId::Stone, "raycast reported the wrong blocking material");
    check(!world.line_of_sight({1, 5}, {30, 5}), "line_of_sight disagreed with a blocked raycast");

    world.set_material({15, 5}, meat2d::MaterialId::Empty);
    const auto reopened = world.raycast({1, 5}, {30, 5});
    check(!reopened.blocked, "destroying the wall did not reopen the raycast");
    check(world.line_of_sight({1, 5}, {30, 5}),
          "line_of_sight did not recover after the wall was destroyed");

    const auto aimed_at_wall = world.raycast({1, 6}, {15, 6});
    check(!aimed_at_wall.blocked, "aiming directly at a wall should not itself count as blocked");
    check(aimed_at_wall.position == meat2d::Vec2i{15, 6}, "raycast at a wall must report the wall cell");
    check(aimed_at_wall.material == meat2d::MaterialId::Stone,
          "raycast did not report the aimed-at wall's material");

    const auto out_of_bounds = world.raycast({-5, 5}, {10, 5});
    check(out_of_bounds.blocked, "out-of-bounds origin must report blocked, not a clear line");
    check(out_of_bounds.position == meat2d::Vec2i{-5, 5},
          "out-of-bounds origin raycast should report the offending endpoint");

    const auto oob_target = world.raycast({10, 5}, {200, 5});
    check(oob_target.blocked, "out-of-bounds target must report blocked, not a clear line");
    check(!world.line_of_sight({-5, 5}, {10, 5}),
          "line_of_sight through an out-of-bounds endpoint must be false");
}

void test_projectile_system_destroys_terrain() {
    meat2d::World world({
        .width = 64,
        .height = 64,
        .seed = 88,
        .sleep_after_ticks = 30,
    });
    for (int y = 0; y < world.height(); ++y) {
        world.set_material({40, y}, meat2d::MaterialId::Stone);
    }

    meat2d::ProjectileSystem projectiles;
    projectiles.spawn(
        {2, 32},
        {
            .velocity = {2, 0},
            .max_ticks = 100,
            .impact_radius = 0,
            .impact_material = meat2d::MaterialId::Empty,
        });

    bool detonated = false;
    for (int tick = 0; tick < 40 && !detonated; ++tick) {
        projectiles.step(world);
        for (const auto& projectile : projectiles.projectiles()) {
            if (!projectile.alive) {
                check(projectile.position == meat2d::Vec2i{40, 32},
                      "projectile did not detonate on the wall it hit");
                detonated = true;
            }
        }
    }
    check(detonated, "projectile never detonated against the wall");
    check(world.material({40, 32}) == meat2d::MaterialId::Empty,
          "projectile impact did not carve the wall");

    projectiles.step(world);
    check(projectiles.projectiles().empty(), "detonated projectile was not pruned next step");
}

void test_projectile_expires_without_impact() {
    meat2d::World world({
        .width = 16,
        .height = 16,
        .seed = 89,
        .sleep_after_ticks = 30,
    });
    meat2d::ProjectileSystem projectiles;
    projectiles.spawn(
        {1, 8},
        {
            .velocity = {1, 0},
            .max_ticks = 3,
            .impact_radius = 0,
            .impact_material = meat2d::MaterialId::Fire,
        });
    for (int tick = 0; tick < 3; ++tick) {
        projectiles.step(world);
    }
    check(projectiles.projectiles().size() == 1,
          "projectile should still be visible on its expiry tick");
    check(!projectiles.projectiles().front().alive, "projectile did not expire at max_ticks");

    projectiles.step(world);
    check(projectiles.projectiles().empty(), "expired projectile was not pruned");
    for (int x = 0; x < world.width(); ++x) {
        for (int y = 0; y < world.height(); ++y) {
            check(world.material({x, y}) == meat2d::MaterialId::Empty,
                  "expiring in open space should not modify terrain");
        }
    }
}

void test_projectile_leaves_world_without_impact() {
    meat2d::World world({
        .width = 16,
        .height = 16,
        .seed = 90,
        .sleep_after_ticks = 30,
    });
    meat2d::ProjectileSystem projectiles;
    projectiles.spawn(
        {14, 8},
        {
            .velocity = {5, 0},
            .max_ticks = 50,
            .impact_radius = 0,
            .impact_material = meat2d::MaterialId::Fire,
        });

    projectiles.step(world);
    check(projectiles.projectiles().size() == 1 && !projectiles.projectiles().front().alive,
          "projectile leaving the world bounds should die without detonating");

    projectiles.step(world);
    check(projectiles.projectiles().empty(), "out-of-bounds projectile was not pruned");
}

void test_replay_round_trip_and_divergence() {
    meat2d::WorldConfig config{
        .width = 48,
        .height = 48,
        .seed = 99,
        .sleep_after_ticks = 30,
    };
    meat2d::World world(config);
    meat2d::replay::ReplayLog log(config);

    // Everything the reference run does to the world besides pure cellular
    // simulation has to go through the log, including initial level dressing
    // — play() only ever reconstructs a bare World from `config`.
    for (std::int32_t y = 0; y < world.height(); ++y) {
        log.record_paint(world.current_tick(), {24, y}, 0, meat2d::MaterialId::Stone);
        world.paint_disc({24, y}, 0, meat2d::MaterialId::Stone);
    }

    constexpr meat2d::Tick total_ticks = 60;
    for (meat2d::Tick tick = 0; tick < total_ticks; ++tick) {
        if (tick == 10) {
            log.record_paint(world.current_tick(), {10, 4}, 3, meat2d::MaterialId::Sand);
            world.paint_disc({10, 4}, 3, meat2d::MaterialId::Sand);
        }
        world.step();
        if (world.current_tick() % 20 == 0) {
            log.record_checkpoint(world.current_tick(), world.state_hash());
        }
    }

    check(!log.paint_events().empty(), "no paint events were recorded");
    check(!log.checkpoints().empty(), "no checkpoints were recorded");

    const auto encoded = log.encode();
    meat2d::replay::ReplayLog decoded;
    check(decoded.decode(encoded), "encoded replay log failed to decode");
    check(decoded.paint_events().size() == log.paint_events().size(),
          "decoded replay lost paint events");
    check(decoded.checkpoints().size() == log.checkpoints().size(),
          "decoded replay lost checkpoints");

    const auto matched = meat2d::replay::play(decoded, total_ticks);
    check(matched.outcome == meat2d::replay::ReplayOutcome::Matched,
          "replay did not reproduce the recorded run");
    check(matched.ticks_played == total_ticks, "matched replay reported the wrong tick count");

    // Rebuild the log with one checkpoint hash deliberately wrong to prove
    // play() detects a divergence at that exact tick, not only "the final
    // state differs".
    const auto tampered_tick = decoded.checkpoints().front().tick;
    const auto original_hash = decoded.checkpoints().front().state_hash;
    meat2d::replay::ReplayLog tampered(decoded.config());
    for (const auto& event : decoded.paint_events()) {
        tampered.record_paint(event.tick, event.position, event.radius, event.material);
    }
    bool tampered_once = false;
    for (const auto& checkpoint : decoded.checkpoints()) {
        if (!tampered_once && checkpoint.tick == tampered_tick) {
            tampered.record_checkpoint(checkpoint.tick, checkpoint.state_hash ^ 0xFFFFFFFFULL);
            tampered_once = true;
        } else {
            tampered.record_checkpoint(checkpoint.tick, checkpoint.state_hash);
        }
    }

    const auto diverged = meat2d::replay::play(tampered, total_ticks);
    check(diverged.outcome == meat2d::replay::ReplayOutcome::Diverged,
          "tampered checkpoint was not detected as a divergence");
    check(diverged.divergent_tick == tampered_tick, "divergence was reported at the wrong tick");
    check(diverged.expected_hash == (original_hash ^ 0xFFFFFFFFULL),
          "divergence reported the wrong expected hash");
    check(diverged.actual_hash == original_hash, "divergence reported the wrong actual hash");
}

void test_replay_decode_sorts_out_of_order_paint_events() {
    meat2d::WorldConfig config{
        .width = 48,
        .height = 48,
        .seed = 7,
        .sleep_after_ticks = 30,
    };

    // Reference run: two paints at ticks 2 and 5, checkpoint at the end.
    meat2d::World world(config);
    meat2d::replay::ReplayLog log(config);
    constexpr meat2d::Tick total_ticks = 12;
    for (meat2d::Tick tick = 0; tick < total_ticks; ++tick) {
        if (world.current_tick() == 2) {
            world.paint_disc({12, 4}, 2, meat2d::MaterialId::Sand);
        }
        if (world.current_tick() == 5) {
            world.paint_disc({30, 4}, 2, meat2d::MaterialId::Water);
        }
        world.step();
    }

    // Record the same events out of order (tick 5 before tick 2). Before
    // decode() sorted by tick, the head-first consumer in play() would
    // never reach the tick-2 event, silently drop both effects, and still
    // return Matched.
    log.record_paint(5, {30, 4}, 2, meat2d::MaterialId::Water);
    log.record_paint(2, {12, 4}, 2, meat2d::MaterialId::Sand);
    log.record_checkpoint(world.current_tick(), world.state_hash());

    meat2d::replay::ReplayLog decoded;
    check(decoded.decode(log.encode()), "out-of-order replay log failed to decode");
    const auto events = decoded.paint_events();
    check(events.size() == 2, "decode dropped paint events");
    check(events[0].tick == 2 && events[1].tick == 5,
          "decode did not sort paint events by tick");

    const auto result = meat2d::replay::play(decoded, total_ticks);
    check(result.outcome == meat2d::replay::ReplayOutcome::Matched,
          "sorted replay did not reproduce the reference run");
}

void test_chunk_store_persistence_across_worlds() {
    const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto directory = std::filesystem::temp_directory_path() / ("meat2d-chunkstore-" + unique);
    std::error_code error;
    std::filesystem::remove_all(directory, error);

    meat2d::World source({
        .width = 128,
        .height = 128,
        .seed = 55,
        .sleep_after_ticks = 30,
    });
    for (int y = 0; y < source.height(); ++y) {
        source.set_material({64, y}, meat2d::MaterialId::Stone);
    }
    source.paint_disc({20, 20}, 8, meat2d::MaterialId::Water);
    for (int tick = 0; tick < 30; ++tick) {
        source.step();
    }

    meat2d::ChunkStore store(directory);
    check(!store.has_chunk(0, 0), "chunk file existed before saving");
    const auto saved = store.save_all(source);
    check(saved == static_cast<std::size_t>(source.chunk_columns() * source.chunk_rows()),
          "save_all did not save every chunk");
    check(store.has_chunk(0, 0), "save_all did not create a file for chunk (0,0)");

    // A freshly constructed World, not the same instance that was stepped
    // and saved — this is the actual persistence scenario: reloading a
    // world in a later process, not just round-tripping in place.
    meat2d::World restored({
        .width = 128,
        .height = 128,
        .seed = 55,
        .sleep_after_ticks = 30,
    });
    const auto loaded = store.load_all(restored);
    check(loaded == saved, "load_all did not load every saved chunk");

    // The disk path must normalize updated_epoch on load exactly like the
    // netcode decode path does; a stale saved epoch equal to the current
    // tick would make update_cell silently skip the restored cell.
    bool epochs_normalized = true;
    for (const auto& chunk : restored.chunks()) {
        for (const auto& cell : chunk.cells) {
            if (cell.updated_epoch != 0) {
                epochs_normalized = false;
            }
        }
    }
    check(epochs_normalized, "load_chunk_cells did not normalize updated_epoch");

    // state_hash() folds in current_tick(), which legitimately differs here
    // (source has stepped 30 times, restored has stepped 0) — chunk_hash()
    // is the tick-independent per-chunk comparison this scenario calls for.
    for (std::size_t index = 0; index < source.chunks().size(); ++index) {
        check(restored.chunk_hash(index) == source.chunk_hash(index),
              "restored chunk did not match the saved chunk's hash");
    }
    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x) {
            check(restored.material({x, y}) == source.material({x, y}),
                  "restored world has a mismatched cell");
        }
    }

    check(!store.load_chunk(restored, -1, 0), "load_chunk accepted an out-of-range column");
    check(!store.load_chunk(restored, source.chunk_columns(), 0),
          "load_chunk accepted a column past the grid");
    check(!store.has_chunk(-1, -1), "has_chunk reported a file for an out-of-range chunk");

    std::filesystem::remove_all(directory, error);
}

void test_parallel_step_deterministic_across_thread_counts() {
    meat2d::World single_threaded({
        .width = 192,
        .height = 128,
        .seed = 91,
        .sleep_after_ticks = 30,
    });
    meat2d::World multi_threaded({
        .width = 192,
        .height = 128,
        .seed = 91,
        .sleep_after_ticks = 30,
    });
    meat2d::seed_sand_lab(single_threaded);
    meat2d::seed_sand_lab(multi_threaded);

    for (int tick = 0; tick < 180; ++tick) {
        single_threaded.step_parallel(1);
        multi_threaded.step_parallel(5);
        check(single_threaded.state_hash() == multi_threaded.state_hash(),
              "step_parallel produced a different result with a different worker count");
        if (failures != 0) {
            return;
        }
    }
}

void test_parallel_step_reproducible_across_runs() {
    meat2d::World first({
        .width = 192,
        .height = 128,
        .seed = 92,
        .sleep_after_ticks = 30,
    });
    meat2d::World second({
        .width = 192,
        .height = 128,
        .seed = 92,
        .sleep_after_ticks = 30,
    });
    meat2d::seed_sand_lab(first);
    meat2d::seed_sand_lab(second);

    for (int tick = 0; tick < 180; ++tick) {
        first.step_parallel(4);
        second.step_parallel(4);
        check(first.state_hash() == second.state_hash(),
              "two identically seeded worlds diverged under step_parallel");
        if (failures != 0) {
            return;
        }
    }
}

void test_parallel_step_conserves_water_and_settles_sand() {
    meat2d::World world({
        .width = 128,
        .height = 96,
        .seed = 93,
        .sleep_after_ticks = 30,
    });
    for (int x = 0; x < world.width(); ++x) {
        world.set_material({x, world.height() - 1}, meat2d::MaterialId::Stone);
    }
    const auto painted_water = world.paint_disc({64, 8}, 6, meat2d::MaterialId::Water);
    world.paint_disc({20, 4}, 4, meat2d::MaterialId::Sand);

    for (int tick = 0; tick < 200; ++tick) {
        world.step_parallel(4);
    }

    std::size_t water_cells = 0;
    bool sand_on_floor = false;
    for (int y = 0; y < world.height(); ++y) {
        for (int x = 0; x < world.width(); ++x) {
            const auto material = world.material({x, y});
            if (material == meat2d::MaterialId::Water) {
                ++water_cells;
            }
            if (material == meat2d::MaterialId::Sand && y == world.height() - 2) {
                sand_on_floor = true;
            }
        }
    }
    check(water_cells == painted_water, "step_parallel changed the water cell count");
    check(sand_on_floor, "step_parallel did not settle sand onto the floor");
}

void test_parallel_step_records_dirty_regions() {
    meat2d::World world({
        .width = 96,
        .height = 96,
        .seed = 94,
        .sleep_after_ticks = 30,
    });
    world.paint_disc({40, 8}, 6, meat2d::MaterialId::Sand);
    world.paint_disc({70, 8}, 5, meat2d::MaterialId::Water);

    const auto pixel_count = 96U * 96U * 4U;
    std::vector<std::uint8_t> partial(pixel_count);
    world.rasterize_rgba(partial);
    world.clear_dirty();

    for (int tick = 0; tick < 24; ++tick) {
        world.step_parallel(3);
    }

    // Same property test_dirty_region_rasterization proves for step(): patch
    // `partial` (a pre-step snapshot) using only the regions step_parallel's
    // deferred mark_changed reported dirty, and it must exactly reproduce a
    // fresh full rasterization — proving the thread_local touch-log merge
    // didn't drop any changed cell.
    bool found_dirty = false;
    for (std::int32_t row = 0; row < world.chunk_rows(); ++row) {
        for (std::int32_t column = 0; column < world.chunk_columns(); ++column) {
            const auto region = world.chunk_dirty_rect(column, row);
            if (region.empty()) {
                continue;
            }
            found_dirty = true;
            check(region.x >= 0 && region.y >= 0 && region.x + region.width <= 96 &&
                      region.y + region.height <= 96,
                  "step_parallel's dirty rect escaped the world bounds");
            world.rasterize_rgba_region(region, partial);
        }
    }
    check(found_dirty, "step_parallel's deferred mark_changed produced no dirty regions");

    std::vector<std::uint8_t> full(pixel_count);
    world.rasterize_rgba(full);
    check(full == partial,
          "step_parallel's dirty-region refresh did not reproduce the full rasterization — "
          "the deferred touch-log merge dropped a changed cell");
}

} // namespace

int main() {
    try {
        test_cell_layout_and_protocol();
        test_fixed_timestep_accumulator();
        test_deterministic_rng();
        test_scene_stack_transitions();
        test_scene_history_undo_redo();
        test_scene_diffs();
        test_scene_snapshots();
        test_scene_entity_components_and_hashing();
        test_scene_hierarchy_and_tags();
        test_tile_map_content_and_serialization();
        test_scene_collision_queries();
        test_kinematic_scene_motion();
        test_rigid_body_step_and_particles();
        test_collision_layers_and_debug_draw();
        test_sprite_batch();
        test_scene_editor_model();
        test_input_state_and_action_map();
        test_camera_transforms_and_clamping();
        test_animation_playback_and_camera_source();
        test_packet_codec();
        test_reliable_sequence_window();
        test_chunk_delta_fragmentation();
        test_udp_loopback();
        test_discovery_codec();
        test_lan_discovery();
        test_public_directory_session();
        test_directory_pagination_identity_and_expiry();
        test_public_browser_distrusts_directory_results();
        test_project_browser_safety_and_editing();
        test_project_manager_validation_and_templates();
        test_sprite_sheet_metadata();
        test_texture_atlas_cache();
        test_authoritative_client_server_session();
        test_prediction_and_reconciliation();
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
        test_dirty_region_rasterization();
        test_raster_output();
        test_raycast_and_line_of_sight();
        test_projectile_system_destroys_terrain();
        test_projectile_expires_without_impact();
        test_projectile_leaves_world_without_impact();
        test_replay_round_trip_and_divergence();
        test_replay_decode_sorts_out_of_order_paint_events();
        test_chunk_store_persistence_across_worlds();
        test_parallel_step_deterministic_across_thread_counts();
        test_parallel_step_reproducible_across_runs();
        test_parallel_step_conserves_water_and_settles_sand();
        test_parallel_step_records_dirty_regions();
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
