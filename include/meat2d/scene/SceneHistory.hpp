#pragma once

#include "meat2d/scene/Scene.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace meat2d::scene {

// Snapshot-backed editor history. Mutations happen through the ordinary Scene
// API; checkpoint() records a valid serialized state and undo/redo restores it.
// This keeps history deterministic and avoids a second command implementation
// that could drift from scene serialization.
class SceneHistory {
  public:
    explicit SceneHistory(Scene initial = Scene{}, std::size_t maximum_entries = 128U);

    [[nodiscard]] Scene& scene() noexcept;
    [[nodiscard]] const Scene& scene() const noexcept;

    // Records the current scene when it differs from the current checkpoint.
    // A new checkpoint after undo discards the redo branch.
    bool checkpoint();
    bool undo();
    bool redo();

    void clear_history();
    [[nodiscard]] std::size_t undo_count() const noexcept;
    [[nodiscard]] std::size_t redo_count() const noexcept;
    [[nodiscard]] std::size_t maximum_entries() const noexcept;

  private:
    bool restore(std::size_t index);

    Scene scene_;
    std::vector<std::vector<std::uint8_t>> snapshots_;
    std::size_t cursor_{};
    std::size_t maximum_entries_{};
};

} // namespace meat2d::scene
