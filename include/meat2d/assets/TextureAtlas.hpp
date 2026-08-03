#pragma once

#include "meat2d/assets/SpriteSheet.hpp"
#include "meat2d/core/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace meat2d::assets {

inline constexpr std::size_t maximum_texture_atlas_entries = 4'096U;

struct AtlasRegion {
    std::uint32_t asset_id{};
    std::string image;
    RectI source{};

    friend bool operator==(const AtlasRegion&, const AtlasRegion&) = default;
};

// Validated metadata cache for renderer backends. It intentionally does not
// own SDL/OpenGL handles; those handles can be rebuilt from the stable image
// path and resolved frame rectangles without changing gameplay state.
class TextureAtlasCache {
  public:
    explicit TextureAtlasCache(std::size_t maximum_entries = maximum_texture_atlas_entries);

    bool define(std::uint32_t asset_id, SpriteSheet sheet, std::uint32_t image_width,
                std::uint32_t image_height);
    bool remove(std::uint32_t asset_id);

    [[nodiscard]] const SpriteSheet* sheet(std::uint32_t asset_id) const noexcept;
    [[nodiscard]] std::optional<AtlasRegion> resolve(std::uint32_t asset_id,
                                                     std::uint32_t frame) const;
    [[nodiscard]] std::uint64_t state_hash() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::size_t maximum_entries() const noexcept;

  private:
    struct Entry {
        std::uint32_t asset_id{};
        SpriteSheet sheet;
        std::uint32_t image_width{};
        std::uint32_t image_height{};
    };

    std::size_t maximum_entries_{};
    std::vector<Entry> entries_;
};

} // namespace meat2d::assets
