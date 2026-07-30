#pragma once

#include "meat2d/core/Types.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace meat2d {
class World;
}

namespace meat2d::render {

// Maintains a CPU-side RGBA8 image of a chunked world and plans the minimal
// set of upload regions each frame by consuming the world's chunk dirty
// bounds. The class is graphics-API free: callers push the returned regions
// with SDL_UpdateTexture, glTexSubImage2D, or any equivalent.
//
// Overlays (entities, cursors, effects) are composited through a per-region
// callback. Cells covered by overlays are marked before update() so their
// chunks refresh on the frames the overlay appears, moves, and disappears.
class WorldView {
  public:
    // Composites overlay pixels into `pixels` (the full-world buffer,
    // row-major RGBA8) restricted to `region`.
    using OverlayPass = std::function<void(std::span<std::uint8_t> pixels, RectI region)>;

    struct Update {
        std::span<const RectI> regions;
        bool full_upload{};
    };

    // Forces the next update() to rasterize and report the whole world.
    // Required when world contents are replaced behind the same address,
    // for example a reset, or when the caller recreates its texture.
    void invalidate() noexcept;

    // Marks the chunk containing `position` as overlay-covered for the next
    // update(). Out-of-bounds positions are ignored.
    void mark_overlay_cell(const World& world, Vec2i position);

    // Rasterizes every changed region of `world` into the internal buffer,
    // runs `overlay` over each refreshed region, consumes the world's dirty
    // bounds, and returns the regions the caller must upload. The returned
    // spans remain valid until the next update() call.
    Update update(World& world, const OverlayPass& overlay = {});

    [[nodiscard]] std::int32_t width() const noexcept;
    [[nodiscard]] std::int32_t height() const noexcept;
    [[nodiscard]] std::int32_t pitch_bytes() const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> pixels() const noexcept;

    // Pointer to the first pixel of `region` inside the full-world buffer.
    // Rows advance by pitch_bytes().
    [[nodiscard]] const std::uint8_t* region_pixels(RectI region) const noexcept;

  private:
    void ensure_capacity(const World& world);
    [[nodiscard]] RectI chunk_rect(std::int32_t column, std::int32_t row) const noexcept;

    const World* tracked_world_{};
    std::int32_t width_{};
    std::int32_t height_{};
    std::int32_t chunk_columns_{};
    std::int32_t chunk_rows_{};
    std::vector<std::uint8_t> pixels_;
    std::vector<std::uint8_t> overlay_now_;
    std::vector<std::uint8_t> overlay_prev_;
    std::vector<RectI> regions_;
    bool full_refresh_{true};
};

} // namespace meat2d::render
