#include "meat2d/tools/SceneEditor.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

namespace meat2d::tools {
namespace {

bool contains(RectI bounds, Vec2i point) noexcept {
    return !bounds.empty() && bounds.contains(point);
}

std::int32_t scaled_size(std::int32_t value, std::int32_t scale) noexcept {
    const auto magnitude = scale < 0 ? -static_cast<std::int64_t>(scale)
                                     : static_cast<std::int64_t>(scale);
    const auto result = static_cast<std::int64_t>(std::max(value, 1)) *
                        std::max<std::int64_t>(1, magnitude);
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(
        result, 1, std::numeric_limits<std::int32_t>::max()));
}

} // namespace

SceneEditor::SceneEditor(scene::Scene initial, std::size_t maximum_history_entries)
    : history_(std::move(initial), maximum_history_entries) {}

scene::Scene& SceneEditor::scene() noexcept { return history_.scene(); }

const scene::Scene& SceneEditor::scene() const noexcept { return history_.scene(); }

scene::SceneHistory& SceneEditor::history() noexcept { return history_; }

const scene::SceneHistory& SceneEditor::history() const noexcept { return history_; }

render::Camera2D& SceneEditor::camera() noexcept { return camera_; }

const render::Camera2D& SceneEditor::camera() const noexcept { return camera_; }

bool SceneEditor::select_at(Vec2i screen) {
    const auto world = camera_.screen_to_world(screen);
    std::optional<scene::EntityId> candidate;
    for (const auto& entity : scene().entities()) {
        if (!contains_at(entity.id, world)) {
            continue;
        }
        if (!candidate || render_layer(entity.id) > render_layer(*candidate) ||
            (render_layer(entity.id) == render_layer(*candidate) && entity.id > *candidate)) {
            candidate = entity.id;
        }
    }
    if (!candidate) {
        clear_selection();
        return false;
    }
    selected_ = candidate;
    return true;
}

bool SceneEditor::select(scene::EntityId entity) {
    if (!scene().contains(entity)) {
        return false;
    }
    selected_ = entity;
    return true;
}

void SceneEditor::clear_selection() noexcept { selected_.reset(); }

std::optional<scene::EntityId> SceneEditor::selected() const noexcept { return selected_; }

std::vector<scene::EntityId> SceneEditor::children_of(scene::EntityId parent) const {
    std::vector<scene::EntityId> result;
    for (const auto& entity : scene().entities()) {
        if (entity.parent == parent) {
            result.push_back(entity.id);
        }
    }
    return result;
}

bool SceneEditor::apply_override(const scene::SceneOverride& scene_override) {
    return apply_overrides(std::span<const scene::SceneOverride>(&scene_override, 1U));
}

bool SceneEditor::apply_overrides(std::span<const scene::SceneOverride> scene_overrides) {
    if (!history_.scene().apply_overrides(scene_overrides) || !history_.checkpoint()) {
        return false;
    }
    if (selected_ && !scene().contains(*selected_)) {
        selected_.reset();
    }
    return true;
}

bool SceneEditor::undo() {
    const auto restored = history_.undo();
    if (selected_ && !scene().contains(*selected_)) {
        selected_.reset();
    }
    return restored;
}

bool SceneEditor::redo() {
    const auto restored = history_.redo();
    if (selected_ && !scene().contains(*selected_)) {
        selected_.reset();
    }
    return restored;
}

bool SceneEditor::contains_at(scene::EntityId entity, Vec2i world) const noexcept {
    const auto* value = scene().find(entity);
    if (value == nullptr || !value->enabled) {
        return false;
    }
    if (const auto collider = scene().world_collider_bounds(entity)) {
        return contains(*collider, world);
    }
    if (!value->sprite) {
        return false;
    }
    const auto position = scene().world_position(entity);
    const auto scale_x = value->transform ? value->transform->scale.x : 1;
    const auto scale_y = value->transform ? value->transform->scale.y : 1;
    return contains({position.x, position.y,
                     scaled_size(value->sprite->source.width, scale_x),
                     scaled_size(value->sprite->source.height, scale_y)},
                    world);
}

std::int16_t SceneEditor::render_layer(scene::EntityId entity) const noexcept {
    const auto* value = scene().find(entity);
    return value != nullptr && value->sprite ? value->sprite->layer : 0;
}

} // namespace meat2d::tools
