#include "meat2d/scene/Scene.hpp"

#include <algorithm>
#include <limits>
#include <type_traits>
#include <utility>

namespace meat2d::scene {
namespace {

constexpr std::uint64_t fnv_offset = 14695981039346656037ULL;
constexpr std::uint64_t fnv_prime = 1099511628211ULL;
constexpr std::uint32_t maximum_text_bytes = 1024U * 1024U;
constexpr std::uint32_t maximum_entities = 1'000'000U;
constexpr std::uint32_t maximum_tags_per_entity = 256U;
constexpr std::size_t maximum_scene_events = 16'384U;

bool valid_tag_text(std::string_view tag) noexcept {
    return !tag.empty() && tag.size() <= maximum_text_bytes;
}

void hash_byte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= value;
    hash *= fnv_prime;
}

template <typename Integer> void hash_integer(std::uint64_t& hash, Integer value) noexcept {
    using Unsigned = std::make_unsigned_t<Integer>;
    const auto converted = static_cast<Unsigned>(value);
    for (std::size_t index = 0; index < sizeof(Integer); ++index) {
        hash_byte(hash, static_cast<std::uint8_t>(converted >> (index * 8U)));
    }
}

void hash_text(std::uint64_t& hash, std::string_view text) noexcept {
    hash_integer(hash, static_cast<std::uint64_t>(text.size()));
    for (const auto character : text) {
        hash_byte(hash, static_cast<std::uint8_t>(character));
    }
}

void hash_rect(std::uint64_t& hash, RectI rect) noexcept {
    hash_integer(hash, rect.x);
    hash_integer(hash, rect.y);
    hash_integer(hash, rect.width);
    hash_integer(hash, rect.height);
}

std::int32_t saturating_i32(std::int64_t value) noexcept {
    return static_cast<std::int32_t>(std::clamp(
        value,
        static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min()),
        static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max())));
}

} // namespace

Scene::Scene(std::string name) : name_(std::move(name)) {}

void Scene::clear() noexcept {
    entities_.clear();
    events_.clear();
    next_entity_id_ = 1;
}

EntityId Scene::create_entity(std::string name) {
    if (next_entity_id_ == invalid_entity ||
        next_entity_id_ == std::numeric_limits<EntityId>::max()) {
        return invalid_entity;
    }
    const auto id = next_entity_id_++;
    entities_.push_back(Entity{
        .id = id,
        .name = std::move(name),
        .enabled = true,
        .parent = invalid_entity,
        .tags = {},
        .transform = std::nullopt,
        .sprite = std::nullopt,
        .collider = std::nullopt,
        .rigid_body = std::nullopt,
    });
    emit_event({.type = SceneEventType::EntityCreated, .entity = id, .tag = {}});
    return id;
}

bool Scene::destroy_entity(EntityId id) {
    if (find(id) == nullptr) {
        return false;
    }

    std::vector<std::pair<EntityId, EntityId>> destroyed;
    for (const auto& entity : entities_) {
        if (is_in_subtree(entity.id, id)) {
            destroyed.emplace_back(entity.id, entity.parent);
        }
    }
    entities_.erase(std::remove_if(entities_.begin(), entities_.end(), [this, id](const Entity& entity) {
                        return is_in_subtree(entity.id, id);
                    }),
                    entities_.end());
    for (auto iterator = destroyed.rbegin(); iterator != destroyed.rend(); ++iterator) {
        emit_event({.type = SceneEventType::EntityDestroyed,
                    .entity = iterator->first,
                    .related = iterator->second,
                    .tag = {}});
    }
    return true;
}

Entity* Scene::find(EntityId id) noexcept {
    const auto iterator = std::find_if(
        entities_.begin(), entities_.end(), [id](const Entity& entity) { return entity.id == id; });
    return iterator == entities_.end() ? nullptr : &*iterator;
}

const Entity* Scene::find(EntityId id) const noexcept {
    const auto iterator = std::find_if(
        entities_.begin(), entities_.end(), [id](const Entity& entity) { return entity.id == id; });
    return iterator == entities_.end() ? nullptr : &*iterator;
}

bool Scene::contains(EntityId id) const noexcept {
    return find(id) != nullptr;
}

bool Scene::set_parent(EntityId child, EntityId parent) {
    auto* child_entity = find(child);
    if (child_entity == nullptr || child == invalid_entity || child == parent) {
        return false;
    }
    if (parent != invalid_entity && find(parent) == nullptr) {
        return false;
    }
    if (child_entity->parent == parent) {
        return true;
    }

    // Walk the proposed parent chain before changing the child. This keeps
    // hierarchy operations deterministic and prevents cycles even when a
    // caller repeatedly reparents entities during editor operations.
    EntityId cursor = parent;
    for (std::size_t depth = 0; cursor != invalid_entity && depth <= entities_.size();
         ++depth) {
        if (cursor == child) {
            return false;
        }
        const auto* ancestor = find(cursor);
        if (ancestor == nullptr) {
            return false;
        }
        cursor = ancestor->parent;
    }
    if (cursor != invalid_entity) {
        return false;
    }
    const auto previous_parent = child_entity->parent;
    child_entity->parent = parent;
    emit_event({.type = SceneEventType::ParentChanged,
                .entity = child,
                .related = parent,
                .previous_related = previous_parent,
                .tag = {}});
    return true;
}

EntityId Scene::parent_of(EntityId child) const noexcept {
    const auto* entity = find(child);
    return entity == nullptr ? invalid_entity : entity->parent;
}

Vec2i Scene::world_position(EntityId id) const noexcept {
    const auto* entity = find(id);
    if (entity == nullptr) {
        return {};
    }
    std::int64_t x = 0;
    std::int64_t y = 0;
    EntityId cursor = id;
    for (std::size_t depth = 0; cursor != invalid_entity && depth <= entities_.size();
         ++depth) {
        const auto* current = find(cursor);
        if (current == nullptr) {
            return {};
        }
        if (current->transform) {
            x += current->transform->position.x;
            y += current->transform->position.y;
        }
        cursor = current->parent;
    }
    if (cursor != invalid_entity) {
        return {};
    }
    return {.x = saturating_i32(x), .y = saturating_i32(y)};
}

bool Scene::add_tag(EntityId id, std::string tag) {
    if (!valid_tag_text(tag)) {
        return false;
    }
    auto* entity = find(id);
    if (entity == nullptr || entity->tags.size() >= maximum_tags_per_entity ||
        std::find(entity->tags.begin(), entity->tags.end(), tag) != entity->tags.end()) {
        return false;
    }
    const auto added_tag = tag;
    entity->tags.push_back(std::move(tag));
    std::sort(entity->tags.begin(), entity->tags.end());
    emit_event({.type = SceneEventType::TagAdded, .entity = id, .tag = added_tag});
    return true;
}

bool Scene::remove_tag(EntityId id, std::string_view tag) {
    auto* entity = find(id);
    if (entity == nullptr) {
        return false;
    }
    const auto iterator = std::find(entity->tags.begin(), entity->tags.end(), tag);
    if (iterator == entity->tags.end()) {
        return false;
    }
    std::string removed(*iterator);
    entity->tags.erase(iterator);
    emit_event({.type = SceneEventType::TagRemoved, .entity = id, .tag = std::move(removed)});
    return true;
}

bool Scene::has_tag(EntityId id, std::string_view tag) const noexcept {
    const auto* entity = find(id);
    return entity != nullptr &&
           std::find(entity->tags.begin(), entity->tags.end(), tag) != entity->tags.end();
}

std::vector<EntityId> Scene::find_tagged(std::string_view tag) const {
    std::vector<EntityId> result;
    for (const auto& entity : entities_) {
        if (std::find(entity.tags.begin(), entity.tags.end(), tag) != entity.tags.end()) {
            result.push_back(entity.id);
        }
    }
    return result;
}

bool Scene::add_group(EntityId id, std::string group) {
    return add_tag(id, std::move(group));
}

bool Scene::remove_group(EntityId id, std::string_view group) {
    return remove_tag(id, group);
}

bool Scene::has_group(EntityId id, std::string_view group) const noexcept {
    return has_tag(id, group);
}

std::vector<EntityId> Scene::find_group(std::string_view group) const {
    return find_tagged(group);
}

void Scene::emit_event(SceneEvent event) {
    if (events_.size() < maximum_scene_events) {
        events_.push_back(std::move(event));
    }
}

std::span<const SceneEvent> Scene::events() const noexcept {
    return std::span<const SceneEvent>(events_);
}

void Scene::clear_events() noexcept {
    events_.clear();
}

bool Scene::is_in_subtree(EntityId entity, EntityId root) const noexcept {
    EntityId cursor = entity;
    for (std::size_t depth = 0; cursor != invalid_entity && depth <= entities_.size();
         ++depth) {
        if (cursor == root) {
            return true;
        }
        const auto* current = find(cursor);
        if (current == nullptr) {
            return false;
        }
        cursor = current->parent;
    }
    return false;
}

std::optional<EntityId> Scene::duplicate_subtree(EntityId source, EntityId parent,
                                                  std::string name) {
    if (find(source) == nullptr ||
        (parent != invalid_entity && (find(parent) == nullptr || is_in_subtree(parent, source)))) {
        return std::nullopt;
    }

    std::vector<EntityId> source_ids;
    for (const auto& entity : entities_) {
        if (is_in_subtree(entity.id, source)) {
            source_ids.push_back(entity.id);
        }
    }
    if (source_ids.empty()) {
        return std::nullopt;
    }

    std::vector<std::pair<EntityId, EntityId>> mapping;
    mapping.reserve(source_ids.size());
    EntityId copied_root = invalid_entity;
    for (const auto old_id : source_ids) {
        const auto* original = find(old_id);
        if (original == nullptr) {
            return std::nullopt;
        }
        // create_entity() may reallocate entities_, so do not retain a pointer
        // into the vector across that call.
        const auto original_copy = *original;
        const auto new_name = old_id == source && !name.empty() ? name : original_copy.name;
        const auto new_id = create_entity(new_name);
        if (new_id == invalid_entity) {
            return std::nullopt;
        }
        auto* copy = find(new_id);
        copy->enabled = original_copy.enabled;
        copy->tags = original_copy.tags;
        copy->transform = original_copy.transform;
        copy->sprite = original_copy.sprite;
        copy->collider = original_copy.collider;
        copy->rigid_body = original_copy.rigid_body;
        mapping.emplace_back(old_id, new_id);
        if (old_id == source) {
            copied_root = new_id;
        }
    }

    for (const auto [old_id, new_id] : mapping) {
        const auto* original = find(old_id);
        EntityId desired_parent = old_id == source ? parent : invalid_entity;
        if (old_id != source && original != nullptr) {
            const auto mapped_parent = std::find_if(
                mapping.begin(), mapping.end(), [original](const auto& value) {
                    return value.first == original->parent;
                });
            desired_parent = mapped_parent == mapping.end() ? invalid_entity : mapped_parent->second;
        }
        if (!set_parent(new_id, desired_parent)) {
            return std::nullopt;
        }
    }
    return copied_root == invalid_entity ? std::nullopt : std::optional<EntityId>(copied_root);
}

std::optional<EntityId> Scene::instantiate_subtree(const Scene& source_scene, EntityId source,
                                                   EntityId parent, std::string name) {
    if (source_scene.find(source) == nullptr ||
        (parent != invalid_entity && find(parent) == nullptr) ||
        (&source_scene == this && parent != invalid_entity && is_in_subtree(parent, source))) {
        return std::nullopt;
    }

    // Copy the source values before creating anything. The destination may
    // be the same scene, and create_entity() can reallocate its entity list.
    std::vector<Entity> source_entities;
    for (const auto& entity : source_scene.entities_) {
        if (source_scene.is_in_subtree(entity.id, source)) {
            source_entities.push_back(entity);
        }
    }
    if (source_entities.empty()) {
        return std::nullopt;
    }

    std::vector<std::pair<EntityId, EntityId>> mapping;
    mapping.reserve(source_entities.size());
    EntityId copied_root = invalid_entity;
    for (const auto& original : source_entities) {
        const auto new_name = original.id == source && !name.empty() ? name : original.name;
        const auto new_id = create_entity(new_name);
        if (new_id == invalid_entity) {
            return std::nullopt;
        }
        auto* copy = find(new_id);
        if (copy == nullptr) {
            return std::nullopt;
        }
        copy->enabled = original.enabled;
        copy->tags = original.tags;
        copy->transform = original.transform;
        copy->sprite = original.sprite;
        copy->collider = original.collider;
        copy->rigid_body = original.rigid_body;
        mapping.emplace_back(original.id, new_id);
        if (original.id == source) {
            copied_root = new_id;
        }
    }

    for (std::size_t index = 0; index < source_entities.size(); ++index) {
        const auto& original = source_entities[index];
        EntityId desired_parent = original.id == source ? parent : invalid_entity;
        if (original.id != source) {
            const auto mapped_parent = std::find_if(
                mapping.begin(), mapping.end(), [&original](const auto& value) {
                    return value.first == original.parent;
                });
            desired_parent = mapped_parent == mapping.end() ? invalid_entity : mapped_parent->second;
        }
        if (!set_parent(mapping[index].second, desired_parent)) {
            return std::nullopt;
        }
    }
    return copied_root == invalid_entity ? std::nullopt : std::optional<EntityId>(copied_root);
}

bool Scene::apply_override(const SceneOverride& scene_override) {
    return apply_overrides(std::span<const SceneOverride>(&scene_override, 1U));
}

bool Scene::apply_overrides(std::span<const SceneOverride> scene_overrides) {
    std::vector<std::size_t> order;
    order.reserve(scene_overrides.size());
    for (std::size_t index = 0; index < scene_overrides.size(); ++index) {
        const auto& scene_override = scene_overrides[index];
        const auto* entity = find(scene_override.entity);
        if (entity == nullptr ||
            std::find_if(order.begin(), order.end(), [&scene_overrides, &scene_override](
                             std::size_t previous) {
                return scene_overrides[previous].entity == scene_override.entity;
            }) != order.end()) {
            return false;
        }
        if (scene_override.parent &&
            (*scene_override.parent != invalid_entity &&
             find(*scene_override.parent) == nullptr)) {
            return false;
        }
        if (scene_override.tags) {
            if (scene_override.tags->size() > maximum_tags_per_entity ||
                std::any_of(scene_override.tags->begin(), scene_override.tags->end(),
                            [](const std::string& tag) { return !valid_tag_text(tag); })) {
                return false;
            }
            auto tags = *scene_override.tags;
            std::sort(tags.begin(), tags.end());
            if (std::adjacent_find(tags.begin(), tags.end()) != tags.end()) {
                return false;
            }
        }
        order.push_back(index);
    }
    // Validate the complete proposed parent graph, not only each override
    // against the current graph. This catches batches such as A -> B and
    // B -> A before any field or lifecycle event is changed.
    for (const auto index : order) {
        const auto& scene_override = scene_overrides[index];
        if (!scene_override.parent) {
            continue;
        }
        EntityId cursor = *scene_override.parent;
        for (std::size_t depth = 0; cursor != invalid_entity && depth <= entities_.size();
             ++depth) {
            if (cursor == scene_override.entity) {
                return false;
            }
            const auto* ancestor = find(cursor);
            if (ancestor == nullptr) {
                return false;
            }
            const auto changed_parent = std::find_if(
                scene_overrides.begin(), scene_overrides.end(), [cursor](const auto& candidate) {
                    return candidate.entity == cursor && candidate.parent.has_value();
                });
            cursor = changed_parent == scene_overrides.end() ? ancestor->parent
                                                              : *changed_parent->parent;
        }
        if (cursor != invalid_entity) {
            return false;
        }
    }
    std::sort(order.begin(), order.end(), [&scene_overrides](std::size_t left, std::size_t right) {
        return scene_overrides[left].entity < scene_overrides[right].entity;
    });

    for (const auto index : order) {
        const auto& scene_override = scene_overrides[index];
        auto* entity = find(scene_override.entity);
        if (scene_override.name) {
            entity->name = *scene_override.name;
        }
        if (scene_override.enabled) {
            entity->enabled = *scene_override.enabled;
        }
        if (scene_override.tags) {
            const auto old_tags = entity->tags;
            for (const auto& old_tag : old_tags) {
                if (std::find(scene_override.tags->begin(), scene_override.tags->end(), old_tag) ==
                    scene_override.tags->end()) {
                    remove_tag(entity->id, old_tag);
                }
            }
            for (const auto& new_tag : *scene_override.tags) {
                if (std::find(old_tags.begin(), old_tags.end(), new_tag) == old_tags.end()) {
                    add_tag(entity->id, new_tag);
                }
            }
        }
        if (scene_override.parent) {
            set_parent(entity->id, *scene_override.parent);
        }

        if (scene_override.transform) {
            if (*scene_override.transform) {
                static_cast<void>(add_transform(entity->id, **scene_override.transform));
            } else {
                remove_transform(entity->id);
            }
        }
        if (scene_override.sprite) {
            if (*scene_override.sprite) {
                static_cast<void>(add_sprite(entity->id, **scene_override.sprite));
            } else {
                remove_sprite(entity->id);
            }
        }
        if (scene_override.collider) {
            if (*scene_override.collider) {
                static_cast<void>(add_collider(entity->id, **scene_override.collider));
            } else {
                remove_collider(entity->id);
            }
        }
        if (scene_override.rigid_body) {
            if (*scene_override.rigid_body) {
                static_cast<void>(add_rigid_body(entity->id, **scene_override.rigid_body));
            } else {
                remove_rigid_body(entity->id);
            }
        }
    }
    return true;
}

bool Scene::hierarchy_valid() const noexcept {
    for (const auto& entity : entities_) {
        EntityId cursor = entity.parent;
        for (std::size_t depth = 0; cursor != invalid_entity && depth <= entities_.size();
             ++depth) {
            const auto* ancestor = find(cursor);
            if (ancestor == nullptr) {
                return false;
            }
            cursor = ancestor->parent;
        }
        if (cursor != invalid_entity) {
            return false;
        }
    }
    return true;
}

std::span<Entity> Scene::entities() noexcept {
    return std::span<Entity>(entities_);
}

std::span<const Entity> Scene::entities() const noexcept {
    return std::span<const Entity>(entities_);
}

std::vector<EntityId> Scene::find_sprites_in_layer(std::int16_t layer,
                                                   bool visible_only) const {
    std::vector<EntityId> result;
    for (const auto& entity : entities_) {
        if (entity.sprite && entity.sprite->layer == layer &&
            (!visible_only || entity.sprite->visible)) {
            result.push_back(entity.id);
        }
    }
    return result;
}

Transform* Scene::add_transform(EntityId id, Transform value) {
    auto* entity = find(id);
    if (entity == nullptr) {
        return nullptr;
    }
    const auto existed = entity->transform.has_value();
    entity->transform = value;
    if (!existed) {
        emit_event({.type = SceneEventType::ComponentAdded,
                    .entity = id,
                    .component = SceneComponent::Transform,
                    .tag = {}});
    }
    return &*entity->transform;
}

Sprite* Scene::add_sprite(EntityId id, Sprite value) {
    auto* entity = find(id);
    if (entity == nullptr) {
        return nullptr;
    }
    const auto existed = entity->sprite.has_value();
    entity->sprite = value;
    if (!existed) {
        emit_event({.type = SceneEventType::ComponentAdded,
                    .entity = id,
                    .component = SceneComponent::Sprite,
                    .tag = {}});
    }
    return &*entity->sprite;
}

Collider* Scene::add_collider(EntityId id, Collider value) {
    auto* entity = find(id);
    if (entity == nullptr) {
        return nullptr;
    }
    const auto existed = entity->collider.has_value();
    entity->collider = value;
    if (!existed) {
        emit_event({.type = SceneEventType::ComponentAdded,
                    .entity = id,
                    .component = SceneComponent::Collider,
                    .tag = {}});
    }
    return &*entity->collider;
}

RigidBody* Scene::add_rigid_body(EntityId id, RigidBody value) {
    auto* entity = find(id);
    if (entity == nullptr) {
        return nullptr;
    }
    const auto existed = entity->rigid_body.has_value();
    entity->rigid_body = value;
    if (!existed) {
        emit_event({.type = SceneEventType::ComponentAdded,
                    .entity = id,
                    .component = SceneComponent::RigidBody,
                    .tag = {}});
    }
    return &*entity->rigid_body;
}

bool Scene::remove_transform(EntityId id) {
    auto* entity = find(id);
    if (entity == nullptr || !entity->transform) {
        return false;
    }
    entity->transform.reset();
    emit_event({.type = SceneEventType::ComponentRemoved,
                .entity = id,
                .component = SceneComponent::Transform,
                .tag = {}});
    return true;
}

bool Scene::remove_sprite(EntityId id) {
    auto* entity = find(id);
    if (entity == nullptr || !entity->sprite) {
        return false;
    }
    entity->sprite.reset();
    emit_event({.type = SceneEventType::ComponentRemoved,
                .entity = id,
                .component = SceneComponent::Sprite,
                .tag = {}});
    return true;
}

bool Scene::remove_collider(EntityId id) {
    auto* entity = find(id);
    if (entity == nullptr || !entity->collider) {
        return false;
    }
    entity->collider.reset();
    emit_event({.type = SceneEventType::ComponentRemoved,
                .entity = id,
                .component = SceneComponent::Collider,
                .tag = {}});
    return true;
}

bool Scene::remove_rigid_body(EntityId id) {
    auto* entity = find(id);
    if (entity == nullptr || !entity->rigid_body) {
        return false;
    }
    entity->rigid_body.reset();
    emit_event({.type = SceneEventType::ComponentRemoved,
                .entity = id,
                .component = SceneComponent::RigidBody,
                .tag = {}});
    return true;
}

std::optional<RectI> Scene::world_collider_bounds(EntityId id) const noexcept {
    const auto* entity = find(id);
    if (entity == nullptr || !entity->collider) {
        return std::nullopt;
    }
    auto bounds = entity->collider->bounds;
    const auto position = world_position(id);
    bounds.x += position.x;
    bounds.y += position.y;
    return bounds;
}

std::vector<EntityId> Scene::query_colliders(RectI area, bool include_sensors) const {
    std::vector<EntityId> result;
    if (area.empty()) {
        return result;
    }
    for (const auto& entity : entities_) {
        if (!include_sensors && entity.collider && entity.collider->sensor) {
            continue;
        }
        const auto bounds = world_collider_bounds(entity.id);
        if (!bounds || bounds->empty()) {
            continue;
        }
        const auto overlaps = static_cast<std::int64_t>(bounds->x) <
                                  static_cast<std::int64_t>(area.x) + area.width &&
                              static_cast<std::int64_t>(area.x) <
                                  static_cast<std::int64_t>(bounds->x) + bounds->width &&
                              static_cast<std::int64_t>(bounds->y) <
                                  static_cast<std::int64_t>(area.y) + area.height &&
                              static_cast<std::int64_t>(area.y) <
                                  static_cast<std::int64_t>(bounds->y) + bounds->height;
        if (overlaps) {
            result.push_back(entity.id);
        }
    }
    return result;
}

const std::string& Scene::name() const noexcept {
    return name_;
}

void Scene::set_name(std::string name) {
    name_ = std::move(name);
}

std::uint64_t Scene::state_hash() const noexcept {
    std::uint64_t hash = fnv_offset;
    hash_text(hash, name_);
    hash_integer(hash, next_entity_id_);
    hash_integer(hash, static_cast<std::uint64_t>(entities_.size()));
    for (const auto& entity : entities_) {
        hash_integer(hash, entity.id);
        hash_text(hash, entity.name);
        hash_byte(hash, static_cast<std::uint8_t>(entity.enabled));
        hash_integer(hash, entity.parent);
        hash_integer(hash, static_cast<std::uint32_t>(entity.tags.size()));
        for (const auto& tag : entity.tags) {
            hash_text(hash, tag);
        }

        hash_byte(hash, static_cast<std::uint8_t>(entity.transform.has_value()));
        if (entity.transform) {
            hash_integer(hash, entity.transform->position.x);
            hash_integer(hash, entity.transform->position.y);
            hash_integer(hash, entity.transform->scale.x);
            hash_integer(hash, entity.transform->scale.y);
        }

        hash_byte(hash, static_cast<std::uint8_t>(entity.sprite.has_value()));
        if (entity.sprite) {
            hash_integer(hash, entity.sprite->asset_id);
            hash_rect(hash, entity.sprite->source);
            hash_integer(hash, entity.sprite->layer);
            hash_byte(hash, static_cast<std::uint8_t>(entity.sprite->visible));
        }

        hash_byte(hash, static_cast<std::uint8_t>(entity.collider.has_value()));
        if (entity.collider) {
            hash_byte(hash, static_cast<std::uint8_t>(entity.collider->shape));
            hash_rect(hash, entity.collider->bounds);
            hash_byte(hash, static_cast<std::uint8_t>(entity.collider->sensor));
            hash_integer(hash, entity.collider->category_bits);
            hash_integer(hash, entity.collider->mask_bits);
        }

        hash_byte(hash, static_cast<std::uint8_t>(entity.rigid_body.has_value()));
        if (entity.rigid_body) {
            hash_integer(hash, entity.rigid_body->velocity.x);
            hash_integer(hash, entity.rigid_body->velocity.y);
            hash_integer(hash, entity.rigid_body->acceleration.x);
            hash_integer(hash, entity.rigid_body->acceleration.y);
            hash_integer(hash, entity.rigid_body->max_velocity.x);
            hash_integer(hash, entity.rigid_body->max_velocity.y);
            hash_byte(hash, static_cast<std::uint8_t>(entity.rigid_body->dynamic));
            hash_byte(hash, static_cast<std::uint8_t>(entity.rigid_body->affected_by_gravity));
        }
    }
    return hash;
}

} // namespace meat2d::scene
