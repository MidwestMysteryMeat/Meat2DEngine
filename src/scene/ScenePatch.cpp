#include "meat2d/scene/Scene.hpp"

#include <algorithm>
#include <utility>

namespace meat2d::scene {

std::vector<SceneDifference> Scene::diff(const Scene& target) const {
    std::vector<SceneDifference> differences;
    for (const auto& entity : entities_) {
        const auto* target_entity = target.find(entity.id);
        if (target_entity == nullptr) {
            differences.push_back(
                {.type = SceneDifferenceType::Removed, .entity = entity.id});
        } else if (entity != *target_entity) {
            differences.push_back(
                {.type = SceneDifferenceType::Changed, .entity = entity.id});
        }
    }
    for (const auto& entity : target.entities_) {
        if (find(entity.id) == nullptr) {
            differences.push_back(
                {.type = SceneDifferenceType::Added, .entity = entity.id});
        }
    }
    std::sort(differences.begin(), differences.end(), [](const SceneDifference& lhs,
                                                        const SceneDifference& rhs) {
        if (lhs.entity != rhs.entity) {
            return lhs.entity < rhs.entity;
        }
        return static_cast<std::uint8_t>(lhs.type) < static_cast<std::uint8_t>(rhs.type);
    });
    return differences;
}

ScenePatch Scene::make_patch(const Scene& target) const {
    ScenePatch patch{
        .scene_name = target.name_,
        .next_entity_id = target.next_entity_id_,
        .base_hash = state_hash(),
        .target_hash = target.state_hash(),
        .operations = {},
    };
    for (const auto& entity : entities_) {
        const auto* target_entity = target.find(entity.id);
        if (target_entity == nullptr) {
            patch.operations.push_back(
                {.type = SceneDifferenceType::Removed, .entity = entity});
        } else if (entity != *target_entity) {
            patch.operations.push_back(
                {.type = SceneDifferenceType::Changed, .entity = *target_entity});
        }
    }
    for (const auto& entity : target.entities_) {
        if (find(entity.id) == nullptr) {
            patch.operations.push_back(
                {.type = SceneDifferenceType::Added, .entity = entity});
        }
    }
    std::sort(patch.operations.begin(), patch.operations.end(),
              [](const ScenePatchOperation& lhs, const ScenePatchOperation& rhs) {
                  if (lhs.entity.id != rhs.entity.id) {
                      return lhs.entity.id < rhs.entity.id;
                  }
                  return static_cast<std::uint8_t>(lhs.type) <
                         static_cast<std::uint8_t>(rhs.type);
              });
    return patch;
}

bool Scene::apply_patch(const ScenePatch& patch) {
    if (patch.base_hash != state_hash() || patch.next_entity_id == invalid_entity) {
        return false;
    }

    Scene candidate = *this;
    candidate.clear_events();
    candidate.name_ = patch.scene_name;
    candidate.next_entity_id_ = patch.next_entity_id;
    std::vector<EntityId> operation_ids;
    operation_ids.reserve(patch.operations.size());
    for (const auto& operation : patch.operations) {
        if (operation.type != SceneDifferenceType::Added &&
            operation.type != SceneDifferenceType::Removed &&
            operation.type != SceneDifferenceType::Changed) {
            return false;
        }
        if (operation.entity.id == invalid_entity ||
            std::find(operation_ids.begin(), operation_ids.end(), operation.entity.id) !=
                operation_ids.end()) {
            return false;
        }
        operation_ids.push_back(operation.entity.id);

        const auto existing = candidate.find(operation.entity.id);
        if (operation.type == SceneDifferenceType::Removed) {
            if (existing == nullptr) {
                return false;
            }
            const auto previous_parent = existing->parent;
            candidate.entities_.erase(
                std::remove_if(candidate.entities_.begin(), candidate.entities_.end(),
                               [id = operation.entity.id](const Entity& entity) {
                                   return entity.id == id;
                               }),
                candidate.entities_.end());
            candidate.emit_event({.type = SceneEventType::EntityDestroyed,
                                  .entity = operation.entity.id,
                                  .related = previous_parent,
                                  .tag = {}});
        } else if (operation.type == SceneDifferenceType::Added) {
            if (existing != nullptr || operation.entity.parent == operation.entity.id) {
                return false;
            }
            candidate.entities_.push_back(operation.entity);
            candidate.emit_event({.type = SceneEventType::EntityCreated,
                                  .entity = operation.entity.id,
                                  .tag = {}});
            if (operation.entity.parent != invalid_entity) {
                candidate.emit_event({.type = SceneEventType::ParentChanged,
                                      .entity = operation.entity.id,
                                      .related = operation.entity.parent,
                                      .previous_related = invalid_entity,
                                      .tag = {}});
            }
        } else {
            if (existing == nullptr || operation.entity.id != existing->id) {
                return false;
            }
            const auto previous = *existing;
            *existing = operation.entity;
            if (previous.parent != existing->parent) {
                candidate.emit_event({.type = SceneEventType::ParentChanged,
                                      .entity = existing->id,
                                      .related = existing->parent,
                                      .previous_related = previous.parent,
                                      .tag = {}});
            }
            const auto emit_component_change = [&candidate, existing](
                                                   SceneComponent component, bool was_present,
                                                   bool is_present) {
                if (was_present == is_present) {
                    return;
                }
                candidate.emit_event({.type = is_present ? SceneEventType::ComponentAdded
                                                          : SceneEventType::ComponentRemoved,
                                      .entity = existing->id,
                                      .component = component,
                                      .tag = {}});
            };
            emit_component_change(SceneComponent::Transform, previous.transform.has_value(),
                                  existing->transform.has_value());
            emit_component_change(SceneComponent::Sprite, previous.sprite.has_value(),
                                  existing->sprite.has_value());
            emit_component_change(SceneComponent::Collider, previous.collider.has_value(),
                                  existing->collider.has_value());
            emit_component_change(SceneComponent::RigidBody, previous.rigid_body.has_value(),
                                  existing->rigid_body.has_value());
            for (const auto& tag : previous.tags) {
                if (std::find(existing->tags.begin(), existing->tags.end(), tag) ==
                    existing->tags.end()) {
                    candidate.emit_event({.type = SceneEventType::TagRemoved,
                                          .entity = existing->id,
                                          .tag = tag});
                }
            }
            for (const auto& tag : existing->tags) {
                if (std::find(previous.tags.begin(), previous.tags.end(), tag) ==
                    previous.tags.end()) {
                    candidate.emit_event({.type = SceneEventType::TagAdded,
                                          .entity = existing->id,
                                          .tag = tag});
                }
            }
        }
    }

    std::vector<EntityId> entity_ids;
    entity_ids.reserve(candidate.entities_.size());
    for (const auto& entity : candidate.entities_) {
        if (entity.id == invalid_entity ||
            std::find(entity_ids.begin(), entity_ids.end(), entity.id) != entity_ids.end()) {
            return false;
        }
        entity_ids.push_back(entity.id);
    }
    if (candidate.serialize().empty() || candidate.state_hash() != patch.target_hash) {
        return false;
    }
    *this = std::move(candidate);
    return true;
}

} // namespace meat2d::scene
