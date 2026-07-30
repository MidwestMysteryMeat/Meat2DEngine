#pragma once

#include "meat2d/sim/Cell.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace meat2d {

inline constexpr std::int32_t chunk_size = 64;
inline constexpr std::size_t cells_per_chunk =
    static_cast<std::size_t>(chunk_size * chunk_size);

struct DirtyBounds {
    std::int16_t min_x{std::numeric_limits<std::int16_t>::max()};
    std::int16_t min_y{std::numeric_limits<std::int16_t>::max()};
    std::int16_t max_x{-1};
    std::int16_t max_y{-1};

    [[nodiscard]] constexpr bool empty() const noexcept {
        return max_x < min_x || max_y < min_y;
    }

    constexpr void include(std::int32_t x, std::int32_t y) noexcept {
        min_x = static_cast<std::int16_t>(x < min_x ? x : min_x);
        min_y = static_cast<std::int16_t>(y < min_y ? y : min_y);
        max_x = static_cast<std::int16_t>(x > max_x ? x : max_x);
        max_y = static_cast<std::int16_t>(y > max_y ? y : max_y);
    }

    constexpr void clear() noexcept {
        *this = {};
    }
};

struct Chunk {
    std::array<Cell, cells_per_chunk> cells{};
    DirtyBounds dirty{};
    std::uint64_t revision{};
    std::uint16_t quiet_ticks{};
    bool active{};
    bool changed{};

    [[nodiscard]] static constexpr std::size_t index(std::int32_t x, std::int32_t y) {
        return static_cast<std::size_t>(y * chunk_size + x);
    }
};

} // namespace meat2d
