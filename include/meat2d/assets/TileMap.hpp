#pragma once

#include "meat2d/core/Types.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace meat2d::assets {

using TileId = std::uint16_t;
inline constexpr TileId empty_tile = 0;
inline constexpr std::uint16_t tile_map_format_version = 1;

struct TileDefinition {
    TileId id{};
    RectI atlas_source{};
    bool solid{};
    std::uint16_t category_bits{1};
    std::uint16_t mask_bits{0xFFFF};
};

struct TileLayerInfo {
    std::string name;
    std::int16_t z{};
    bool visible{true};
};

// Renderer-neutral tile content. Atlas coordinates and collision metadata stay
// in the content layer so SDL, editor, and headless/server consumers share the
// same map document and deterministic collision queries.
class TileMap {
  public:
    explicit TileMap(std::int32_t width = 0, std::int32_t height = 0,
                     std::uint16_t layer_count = 1);

    bool resize(std::int32_t width, std::int32_t height,
                std::uint16_t layer_count = 1);
    [[nodiscard]] std::int32_t width() const noexcept;
    [[nodiscard]] std::int32_t height() const noexcept;
    [[nodiscard]] std::uint16_t layer_count() const noexcept;
    [[nodiscard]] bool in_bounds(Vec2i cell) const noexcept;

    bool add_layer(std::string name, std::int16_t z = 0, bool visible = true);
    [[nodiscard]] const TileLayerInfo* layer_info(std::uint16_t layer) const noexcept;
    [[nodiscard]] std::span<const TileId> layer_tiles(std::uint16_t layer) const noexcept;

    bool define_tile(TileDefinition definition);
    [[nodiscard]] const TileDefinition* tile_definition(TileId id) const noexcept;
    bool set_tile(std::uint16_t layer, Vec2i cell, TileId id) noexcept;
    [[nodiscard]] TileId tile(std::uint16_t layer, Vec2i cell) const noexcept;

    // Returns solid cells in row-major order, clipped to the requested area.
    // Each returned RectI is one map cell in local map coordinates.
    [[nodiscard]] std::vector<RectI> solid_cells(std::uint16_t layer,
                                                 RectI area) const;

    [[nodiscard]] std::uint64_t state_hash() const noexcept;
    [[nodiscard]] std::vector<std::uint8_t> serialize() const;
    [[nodiscard]] static std::optional<TileMap> deserialize(
        std::span<const std::uint8_t> bytes);

  private:
    struct Layer {
        TileLayerInfo info;
        std::vector<TileId> tiles;
    };

    [[nodiscard]] std::size_t offset(Vec2i cell) const noexcept;
    [[nodiscard]] bool valid_dimensions(std::int32_t width, std::int32_t height,
                                        std::uint16_t layer_count) const noexcept;
    [[nodiscard]] bool valid_tile_references() const noexcept;

    std::int32_t width_{};
    std::int32_t height_{};
    std::vector<TileDefinition> definitions_;
    std::vector<Layer> layers_;
};

} // namespace meat2d::assets
