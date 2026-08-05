#pragma once

#include "meat2d/render/Camera.hpp"
#include "meat2d/scene/SceneHistory.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace meat2d::tools {

// Backend-neutral editor state for a scene tree and viewport. The launcher can
// render this state with ImGui while headless tools and tests use the same
// selection, override, and history semantics.
class SceneEditor {
  public:
    explicit SceneEditor(scene::Scene initial = scene::Scene{},
                         std::size_t maximum_history_entries = 128U);

    [[nodiscard]] scene::Scene& scene() noexcept;
    [[nodiscard]] const scene::Scene& scene() const noexcept;
    [[nodiscard]] scene::SceneHistory& history() noexcept;
    [[nodiscard]] const scene::SceneHistory& history() const noexcept;
    [[nodiscard]] render::Camera2D& camera() noexcept;
    [[nodiscard]] const render::Camera2D& camera() const noexcept;

    bool select_at(Vec2i screen);
    bool select(scene::EntityId entity);
    void clear_selection() noexcept;
    [[nodiscard]] std::optional<scene::EntityId> selected() const noexcept;
    [[nodiscard]] std::vector<scene::EntityId> children_of(
        scene::EntityId parent = scene::invalid_entity) const;

    bool apply_override(const scene::SceneOverride& scene_override);
    bool apply_overrides(std::span<const scene::SceneOverride> scene_overrides);
    bool undo();
    bool redo();

  private:
    [[nodiscard]] bool contains_at(scene::EntityId entity, Vec2i world) const noexcept;
    [[nodiscard]] std::int16_t render_layer(scene::EntityId entity) const noexcept;

    scene::SceneHistory history_;
    render::Camera2D camera_;
    std::optional<scene::EntityId> selected_;
};

} // namespace meat2d::tools
