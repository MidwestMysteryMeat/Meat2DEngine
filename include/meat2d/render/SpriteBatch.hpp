#pragma once

#include "meat2d/render/Camera.hpp"
#include "meat2d/scene/Scene.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace meat2d::render {

inline constexpr std::size_t maximum_sprite_commands = 65'536U;

struct SpriteDrawCommand {
    scene::EntityId entity{};
    std::uint32_t asset_id{};
    RectI source{};
    RectI destination{};
    std::int16_t layer{};
    bool flip_x{};
    bool flip_y{};

    friend bool operator==(const SpriteDrawCommand&, const SpriteDrawCommand&) = default;
};

// Backend-neutral sprite submission. It performs deterministic camera culling
// and layer/entity ordering; SDL, OpenGL, or another backend owns textures and
// consumes the resulting commands.
class SpriteBatch {
  public:
    explicit SpriteBatch(std::size_t maximum_commands = maximum_sprite_commands);

    // Rebuilds the bounded command list. Returns false without changing the
    // previous list if the scene exceeds the configured command budget.
    bool build(const scene::Scene& scene, const Camera2D& camera,
               bool visible_only = true);
    void clear() noexcept;

    [[nodiscard]] std::span<const SpriteDrawCommand> commands() const noexcept;
    [[nodiscard]] std::size_t maximum_commands() const noexcept;

  private:
    std::size_t maximum_commands_{};
    std::vector<SpriteDrawCommand> commands_;
};

} // namespace meat2d::render
