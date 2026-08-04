#include "meat2d/scene/Scene.hpp"

#include <algorithm>
#include <limits>
#include <type_traits>
#include <utility>

namespace meat2d::scene {
namespace {

constexpr std::uint64_t fnv_offset = 14695981039346656037ULL;
constexpr std::uint64_t fnv_prime = 1099511628211ULL;
constexpr std::uint32_t maximum_entities = 1'000'000U;
constexpr std::size_t maximum_scene_events = 16'384U;

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
