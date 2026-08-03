#pragma once

#include "meat2d/core/Types.hpp"

#include <cstdint>

namespace meat2d::render {

// Integer camera transform shared by headless gameplay and graphical clients.
// A zoom of 100 means one world unit maps to one viewport pixel.
class Camera2D {
  public:
    [[nodiscard]] Vec2i center() const noexcept;
    void set_center(Vec2i center) noexcept;

    [[nodiscard]] Vec2i viewport() const noexcept;
    void set_viewport(Vec2i viewport) noexcept;

    [[nodiscard]] std::int32_t zoom_percent() const noexcept;
    void set_zoom_percent(std::int32_t zoom_percent) noexcept;

    [[nodiscard]] RectI visible_rect() const noexcept;
    [[nodiscard]] Vec2i world_to_screen(Vec2i world) const noexcept;
    [[nodiscard]] Vec2i screen_to_world(Vec2i screen) const noexcept;

    void clamp_to(RectI world_bounds) noexcept;

  private:
    Vec2i center_{};
    Vec2i viewport_{320, 180};
    std::int32_t zoom_percent_{100};
};

} // namespace meat2d::render
