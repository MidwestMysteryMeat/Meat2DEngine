#include "meat2d/render/DebugDraw.hpp"

#include <algorithm>

namespace meat2d::render {

DebugDrawList::DebugDrawList(std::size_t maximum_primitives)
    : maximum_primitives_(maximum_primitives) {
    primitives_.reserve(maximum_primitives_);
}

bool DebugDrawList::add_line(Vec2i start, Vec2i end, Rgba8 color, std::uint16_t thickness) {
    if (!can_add()) {
        return false;
    }
    primitives_.push_back({
        .type = DebugPrimitiveType::Line,
        .start = start,
        .end = end,
        .thickness = std::max<std::uint16_t>(1, thickness),
        .color = color,
        .text = {},
    });
    return true;
}

bool DebugDrawList::add_rectangle(RectI rectangle, Rgba8 color, std::uint16_t thickness) {
    if (!can_add()) {
        return false;
    }
    primitives_.push_back({
        .type = DebugPrimitiveType::Rectangle,
        .rectangle = rectangle,
        .thickness = std::max<std::uint16_t>(1, thickness),
        .color = color,
        .text = {},
    });
    return true;
}

bool DebugDrawList::add_circle(Vec2i center, std::int32_t radius, Rgba8 color,
                               std::uint16_t thickness) {
    if (!can_add() || radius < 0) {
        return false;
    }
    primitives_.push_back({
        .type = DebugPrimitiveType::Circle,
        .start = center,
        .radius = radius,
        .thickness = std::max<std::uint16_t>(1, thickness),
        .color = color,
        .text = {},
    });
    return true;
}

bool DebugDrawList::add_text(Vec2i position, std::string_view text, Rgba8 color) {
    if (!can_add()) {
        return false;
    }
    primitives_.push_back({
        .type = DebugPrimitiveType::Text,
        .start = position,
        .color = color,
        .text = std::string(text),
    });
    return true;
}

void DebugDrawList::clear() noexcept {
    primitives_.clear();
}

std::span<const DebugPrimitive> DebugDrawList::primitives() const noexcept {
    return primitives_;
}

std::size_t DebugDrawList::maximum_primitives() const noexcept {
    return maximum_primitives_;
}

bool DebugDrawList::can_add() const noexcept {
    return primitives_.size() < maximum_primitives_;
}

} // namespace meat2d::render
