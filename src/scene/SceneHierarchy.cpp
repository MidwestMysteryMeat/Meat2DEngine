#include "meat2d/scene/Scene.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace meat2d::scene {
namespace {

constexpr std::uint32_t maximum_tags_per_entity = 256U;
constexpr std::uint32_t maximum_text_bytes = 1024U * 1024U;
constexpr std::size_t maximum_scene_events = 16'384U;

bool valid_tag_text(std::string_view tag) noexcept {
    return !tag.empty() && tag.size() <= maximum_text_bytes;
}

std::int32_t saturating_i32(std::int64_t value) noexcept {
    return static_cast<std::int32_t>(std::clamp(
        value,
        static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min()),
        static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max())));
}

} // namespace

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

} // namespace meat2d::scene
