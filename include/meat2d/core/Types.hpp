#pragma once

#include <algorithm>
#include <cstdint>

namespace meat2d {

using Tick = std::uint64_t;

struct Vec2i {
    std::int32_t x{};
    std::int32_t y{};

    friend constexpr bool operator==(Vec2i, Vec2i) = default;
};

struct RectI {
    std::int32_t x{};
    std::int32_t y{};
    std::int32_t width{};
    std::int32_t height{};

    [[nodiscard]] constexpr bool empty() const noexcept {
        return width <= 0 || height <= 0;
    }

    [[nodiscard]] constexpr bool contains(Vec2i point) const noexcept {
        return point.x >= x && point.y >= y && point.x < x + width && point.y < y + height;
    }

    friend constexpr bool operator==(RectI, RectI) = default;
};

struct Rgba8 {
    std::uint8_t r{};
    std::uint8_t g{};
    std::uint8_t b{};
    std::uint8_t a{255};

    friend constexpr bool operator==(Rgba8, Rgba8) = default;
};

} // namespace meat2d
