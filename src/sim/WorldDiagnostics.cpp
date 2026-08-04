#include "meat2d/sim/World.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace meat2d {
namespace {

constexpr std::uint64_t fnv_offset = 14695981039346656037ULL;
constexpr std::uint64_t fnv_prime = 1099511628211ULL;
constexpr std::array<Vec2i, 4> cardinal_directions{{
    {0, -1},
    {1, 0},
    {0, 1},
    {-1, 0},
}};

void hash_byte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= value;
    hash *= fnv_prime;
}

void hash_u64(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (int shift = 0; shift < 64; shift += 8) {
        hash_byte(hash, static_cast<std::uint8_t>(value >> shift));
    }
}

std::uint8_t shade(std::uint8_t channel, int delta) noexcept {
    return static_cast<std::uint8_t>(std::clamp(static_cast<int>(channel) + delta, 0, 255));
}

Rgba8 cell_rgba(const Cell& value) noexcept {
    auto color = material_definition(value.material).color;
    if (value.material != MaterialId::Empty) {
        const int variation = static_cast<int>(value.variant) - 8;
        color.r = shade(color.r, variation);
        color.g = shade(color.g, variation);
        color.b = shade(color.b, variation);
    }
    if (value.material == MaterialId::Metal && value.state > 0) {
        color.b = shade(color.b, 70);
        color.g = shade(color.g, 30);
    }
    return color;
}

} // namespace

std::uint64_t World::state_hash() const noexcept {
    std::uint64_t hash = fnv_offset;
    hash_u64(hash, static_cast<std::uint64_t>(config_.width));
    hash_u64(hash, static_cast<std::uint64_t>(config_.height));
    hash_u64(hash, config_.seed);
    hash_u64(hash, tick_);

    for (std::int32_t y = 0; y < config_.height; ++y) {
        for (std::int32_t x = 0; x < config_.width; ++x) {
            const auto& value = cell_unchecked({x, y});
            hash_byte(hash, static_cast<std::uint8_t>(value.material));
            hash_byte(hash, value.variant);
            hash_byte(hash, value.state);
            const auto temperature = static_cast<std::uint16_t>(value.temperature);
            hash_byte(hash, static_cast<std::uint8_t>(temperature));
            hash_byte(hash, static_cast<std::uint8_t>(temperature >> 8U));
            hash_byte(hash, static_cast<std::uint8_t>(value.velocity_x));
            hash_byte(hash, static_cast<std::uint8_t>(value.velocity_y));
        }
    }
    return hash;
}

std::uint64_t World::chunk_hash(std::size_t chunk_index) const noexcept {
    if (chunk_index >= chunks_.size()) {
        return 0;
    }
    std::uint64_t hash = fnv_offset;
    for (const auto& value : chunks_[chunk_index].cells) {
        hash_byte(hash, static_cast<std::uint8_t>(value.material));
        hash_byte(hash, value.variant);
        hash_byte(hash, value.state);
        const auto temperature = static_cast<std::uint16_t>(value.temperature);
        hash_byte(hash, static_cast<std::uint8_t>(temperature));
        hash_byte(hash, static_cast<std::uint8_t>(temperature >> 8U));
        hash_byte(hash, static_cast<std::uint8_t>(value.velocity_x));
        hash_byte(hash, static_cast<std::uint8_t>(value.velocity_y));
    }
    return hash;
}

void World::rasterize_rgba(std::span<std::uint8_t> destination) const {
    rasterize_rgba_region({0, 0, config_.width, config_.height}, destination);
}

void World::rasterize_rgba_region(RectI region, std::span<std::uint8_t> destination) const {
    const auto required =
        static_cast<std::size_t>(config_.width) * static_cast<std::size_t>(config_.height) * 4U;
    if (destination.size() < required) {
        throw std::invalid_argument("RGBA destination is smaller than the world");
    }

    const auto begin_x = std::max<std::int32_t>(region.x, 0);
    const auto begin_y = std::max<std::int32_t>(region.y, 0);
    const auto end_x = std::min<std::int32_t>(region.x + region.width, config_.width);
    const auto end_y = std::min<std::int32_t>(region.y + region.height, config_.height);
    for (std::int32_t y = begin_y; y < end_y; ++y) {
        auto output = (static_cast<std::size_t>(y) * static_cast<std::size_t>(config_.width) +
                       static_cast<std::size_t>(begin_x)) *
                      4U;
        for (std::int32_t x = begin_x; x < end_x; ++x) {
            const auto color = cell_rgba(cell_unchecked({x, y}));
            destination[output++] = color.r;
            destination[output++] = color.g;
            destination[output++] = color.b;
            destination[output++] = color.a;
        }
    }
}

std::int32_t World::chunk_columns() const noexcept {
    return chunk_columns_;
}

std::int32_t World::chunk_rows() const noexcept {
    return chunk_rows_;
}

std::span<const Chunk> World::chunks() const noexcept {
    return chunks_;
}

RectI World::chunk_dirty_rect(std::int32_t column, std::int32_t row) const noexcept {
    if (column < 0 || row < 0 || column >= chunk_columns_ || row >= chunk_rows_) {
        return {};
    }
    const auto& bounds = chunks_[static_cast<std::size_t>(row * chunk_columns_ + column)].dirty;
    if (bounds.empty()) {
        return {};
    }
    const auto begin_x = column * chunk_size + static_cast<std::int32_t>(bounds.min_x);
    const auto begin_y = row * chunk_size + static_cast<std::int32_t>(bounds.min_y);
    const auto end_x = std::min(column * chunk_size + static_cast<std::int32_t>(bounds.max_x) + 1,
                                config_.width);
    const auto end_y =
        std::min(row * chunk_size + static_cast<std::int32_t>(bounds.max_y) + 1, config_.height);
    if (begin_x >= end_x || begin_y >= end_y) {
        return {};
    }
    return {begin_x, begin_y, end_x - begin_x, end_y - begin_y};
}

std::span<const Cell> World::chunk_cells(std::int32_t column, std::int32_t row) const noexcept {
    if (column < 0 || row < 0 || column >= chunk_columns_ || row >= chunk_rows_) {
        return {};
    }
    return chunks_[static_cast<std::size_t>(row * chunk_columns_ + column)].cells;
}

bool World::load_chunk_cells(
    std::int32_t column,
    std::int32_t row,
    std::span<const Cell> cells) noexcept {
    if (column < 0 || row < 0 || column >= chunk_columns_ || row >= chunk_rows_ ||
        cells.size() != cells_per_chunk) {
        return false;
    }

    auto& chunk = chunks_[static_cast<std::size_t>(row * chunk_columns_ + column)];
    std::copy(cells.begin(), cells.end(), chunk.cells.begin());
    // Normalize the epoch just like the netcode decode path does: a saved
    // epoch that happens to equal the current tick would make update_cell
    // skip the restored cell for one tick.
    for (auto& value : chunk.cells) {
        value.updated_epoch = 0;
    }
    chunk.active = true;
    chunk.changed = true;
    chunk.quiet_ticks = 0;
    ++chunk.revision;
    chunk.dirty.clear();
    chunk.dirty.include(0, 0);
    chunk.dirty.include(chunk_size - 1, chunk_size - 1);

    for (const auto& offset : cardinal_directions) {
        const auto neighbor_column = column + offset.x;
        const auto neighbor_row = row + offset.y;
        if (neighbor_column < 0 || neighbor_row < 0 || neighbor_column >= chunk_columns_ ||
            neighbor_row >= chunk_rows_) {
            continue;
        }
        auto& neighbor =
            chunks_[static_cast<std::size_t>(neighbor_row * chunk_columns_ + neighbor_column)];
        neighbor.active = true;
        neighbor.quiet_ticks = 0;
    }

    return true;
}

} // namespace meat2d
