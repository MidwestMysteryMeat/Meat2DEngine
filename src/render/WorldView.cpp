#include "meat2d/render/WorldView.hpp"

#include "meat2d/sim/Chunk.hpp"
#include "meat2d/sim/World.hpp"

#include <algorithm>

namespace meat2d::render {

void WorldView::invalidate() noexcept {
    full_refresh_ = true;
}

void WorldView::mark_overlay_cell(const World& world, Vec2i position) {
    ensure_capacity(world);
    if (position.x < 0 || position.y < 0 || position.x >= width_ || position.y >= height_) {
        return;
    }
    overlay_now_[static_cast<std::size_t>((position.y / chunk_size) * chunk_columns_ +
                                          position.x / chunk_size)] = 1U;
}

WorldView::Update WorldView::update(World& world, const OverlayPass& overlay) {
    ensure_capacity(world);
    regions_.clear();

    const bool full_upload = full_refresh_;
    if (full_refresh_) {
        world.rasterize_rgba(pixels_);
        const RectI everything{0, 0, width_, height_};
        if (overlay) {
            overlay(pixels_, everything);
        }
        regions_.push_back(everything);
        full_refresh_ = false;
    } else {
        for (std::int32_t row = 0; row < chunk_rows_; ++row) {
            for (std::int32_t column = 0; column < chunk_columns_; ++column) {
                const auto chunk_index = static_cast<std::size_t>(row * chunk_columns_ + column);
                const bool overlay_refresh =
                    overlay_now_[chunk_index] != 0U || overlay_prev_[chunk_index] != 0U;
                const auto region = overlay_refresh ? chunk_rect(column, row)
                                                    : world.chunk_dirty_rect(column, row);
                if (region.empty()) {
                    continue;
                }
                world.rasterize_rgba_region(region, pixels_);
                if (overlay) {
                    overlay(pixels_, region);
                }
                regions_.push_back(region);
            }
        }
    }

    world.clear_dirty();
    overlay_prev_ = overlay_now_;
    std::fill(overlay_now_.begin(), overlay_now_.end(), 0U);
    return {regions_, full_upload};
}

std::int32_t WorldView::width() const noexcept {
    return width_;
}

std::int32_t WorldView::height() const noexcept {
    return height_;
}

std::int32_t WorldView::pitch_bytes() const noexcept {
    return width_ * 4;
}

std::span<const std::uint8_t> WorldView::pixels() const noexcept {
    return pixels_;
}

const std::uint8_t* WorldView::region_pixels(RectI region) const noexcept {
    return pixels_.data() + (static_cast<std::size_t>(region.y) * static_cast<std::size_t>(width_) +
                             static_cast<std::size_t>(region.x)) *
                                4U;
}

void WorldView::ensure_capacity(const World& world) {
    if (tracked_world_ == &world && width_ == world.width() && height_ == world.height()) {
        return;
    }
    tracked_world_ = &world;
    width_ = world.width();
    height_ = world.height();
    chunk_columns_ = world.chunk_columns();
    chunk_rows_ = world.chunk_rows();
    pixels_.assign(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4U, 0U);
    overlay_now_.assign(static_cast<std::size_t>(chunk_columns_ * chunk_rows_), 0U);
    overlay_prev_.assign(overlay_now_.size(), 0U);
    full_refresh_ = true;
}

RectI WorldView::chunk_rect(std::int32_t column, std::int32_t row) const noexcept {
    const auto x = column * chunk_size;
    const auto y = row * chunk_size;
    return {
        x,
        y,
        std::min(chunk_size, width_ - x),
        std::min(chunk_size, height_ - y),
    };
}

} // namespace meat2d::render
