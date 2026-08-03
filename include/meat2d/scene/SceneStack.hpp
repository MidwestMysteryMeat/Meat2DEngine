#pragma once

#include "meat2d/scene/Scene.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace meat2d::scene {

enum class SceneTransitionType : std::uint8_t { Replace, Push, Pop };

struct SceneTransition {
    SceneTransitionType type{};
    std::string from;
    std::string to;
};

// Owns named scenes and a deterministic active stack. Scene content remains
// ordinary Scene data; this class only defines shared flow semantics for menus,
// rooms, dialogue, pause overlays, and lobby/game transitions.
class SceneStack {
  public:
    bool register_scene(std::string name, Scene scene);
    bool unregister_scene(std::string_view name);
    [[nodiscard]] bool has_scene(std::string_view name) const noexcept;

    bool replace(std::string_view name);
    bool push(std::string_view name);
    bool pop();

    [[nodiscard]] Scene* active() noexcept;
    [[nodiscard]] const Scene* active() const noexcept;
    [[nodiscard]] std::string_view active_name() const noexcept;
    [[nodiscard]] Scene* find(std::string_view name) noexcept;
    [[nodiscard]] const Scene* find(std::string_view name) const noexcept;

    [[nodiscard]] std::size_t depth() const noexcept;
    [[nodiscard]] std::span<const SceneTransition> transitions() const noexcept;
    void clear_transitions() noexcept;

  private:
    struct Entry {
        std::string name;
        Scene scene;
    };

    [[nodiscard]] std::size_t find_index(std::string_view name) const noexcept;
    void record_transition(SceneTransition transition);

    std::vector<Entry> scenes_;
    std::vector<std::size_t> stack_;
    std::vector<SceneTransition> transitions_;
};

} // namespace meat2d::scene
