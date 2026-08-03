#pragma once

#include "meat2d/core/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace meat2d::render {

enum class DebugPrimitiveType : std::uint8_t { Line, Rectangle, Circle, Text };

struct DebugPrimitive {
    DebugPrimitiveType type{};
    Vec2i start{};
    Vec2i end{};
    RectI rectangle{};
    std::int32_t radius{};
    std::uint16_t thickness{1};
    Rgba8 color{};
    std::string text;
};

class DebugDrawList {
  public:
    explicit DebugDrawList(std::size_t maximum_primitives = 8192);

    bool add_line(Vec2i start, Vec2i end, Rgba8 color = {}, std::uint16_t thickness = 1);
    bool add_rectangle(RectI rectangle, Rgba8 color = {}, std::uint16_t thickness = 1);
    bool add_circle(Vec2i center, std::int32_t radius, Rgba8 color = {},
                    std::uint16_t thickness = 1);
    bool add_text(Vec2i position, std::string_view text, Rgba8 color = {});

    void clear() noexcept;
    [[nodiscard]] std::span<const DebugPrimitive> primitives() const noexcept;
    [[nodiscard]] std::size_t maximum_primitives() const noexcept;

  private:
    [[nodiscard]] bool can_add() const noexcept;

    std::size_t maximum_primitives_{};
    std::vector<DebugPrimitive> primitives_;
};

} // namespace meat2d::render
