#include "meat2d/render/Camera.hpp"

#include <algorithm>
#include <limits>

namespace meat2d::render {
namespace {

std::int32_t clamp_i64(std::int64_t value) noexcept {
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(
        value, std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max()));
}

std::int32_t visible_extent(std::int32_t viewport, std::int32_t zoom) noexcept {
    if (viewport <= 0) {
        return 0;
    }
    const auto extent = static_cast<std::int64_t>(viewport) * 100LL / zoom;
    return std::max<std::int32_t>(1, clamp_i64(extent));
}

} // namespace

Vec2i Camera2D::center() const noexcept {
    return center_;
}

void Camera2D::set_center(Vec2i center) noexcept {
    center_ = center;
}

Vec2i Camera2D::viewport() const noexcept {
    return viewport_;
}

void Camera2D::set_viewport(Vec2i viewport) noexcept {
    viewport_.x = std::max<std::int32_t>(1, viewport.x);
    viewport_.y = std::max<std::int32_t>(1, viewport.y);
}

std::int32_t Camera2D::zoom_percent() const noexcept {
    return zoom_percent_;
}

void Camera2D::set_zoom_percent(std::int32_t zoom_percent) noexcept {
    zoom_percent_ = std::clamp<std::int32_t>(zoom_percent, 1, 1000);
}

RectI Camera2D::visible_rect() const noexcept {
    const auto width = visible_extent(viewport_.x, zoom_percent_);
    const auto height = visible_extent(viewport_.y, zoom_percent_);
    return {
        clamp_i64(static_cast<std::int64_t>(center_.x) - width / 2),
        clamp_i64(static_cast<std::int64_t>(center_.y) - height / 2),
        width,
        height,
    };
}

Vec2i Camera2D::world_to_screen(Vec2i world) const noexcept {
    const auto visible = visible_rect();
    return {
        clamp_i64((static_cast<std::int64_t>(world.x) - visible.x) * zoom_percent_ / 100),
        clamp_i64((static_cast<std::int64_t>(world.y) - visible.y) * zoom_percent_ / 100),
    };
}

Vec2i Camera2D::screen_to_world(Vec2i screen) const noexcept {
    const auto visible = visible_rect();
    return {
        clamp_i64(static_cast<std::int64_t>(visible.x) +
                  static_cast<std::int64_t>(screen.x) * 100 / zoom_percent_),
        clamp_i64(static_cast<std::int64_t>(visible.y) +
                  static_cast<std::int64_t>(screen.y) * 100 / zoom_percent_),
    };
}

void Camera2D::clamp_to(RectI world_bounds) noexcept {
    if (world_bounds.empty()) {
        return;
    }
    const auto visible = visible_rect();
    if (visible.width >= world_bounds.width) {
        center_.x = world_bounds.x + world_bounds.width / 2;
    } else {
        const auto left_extent = visible.width / 2;
        const auto right_extent = visible.width - left_extent;
        center_.x = std::clamp(center_.x, world_bounds.x + left_extent,
                               world_bounds.x + world_bounds.width - right_extent);
    }
    if (visible.height >= world_bounds.height) {
        center_.y = world_bounds.y + world_bounds.height / 2;
    } else {
        const auto top_extent = visible.height / 2;
        const auto bottom_extent = visible.height - top_extent;
        center_.y = std::clamp(center_.y, world_bounds.y + top_extent,
                               world_bounds.y + world_bounds.height - bottom_extent);
    }
}

} // namespace meat2d::render
