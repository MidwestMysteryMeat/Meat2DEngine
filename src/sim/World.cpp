#include "meat2d/sim/World.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace meat2d {
namespace {

constexpr std::uint64_t fnv_offset = 14695981039346656037ULL;
constexpr std::uint64_t fnv_prime = 1099511628211ULL;

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

} // namespace

World::World(WorldConfig config) : config_(config) {
    if (config_.width <= 0 || config_.height <= 0) {
        throw std::invalid_argument("world dimensions must be positive");
    }
    if (config_.sleep_after_ticks == 0) {
        throw std::invalid_argument("sleep_after_ticks must be positive");
    }

    chunk_columns_ = (config_.width + chunk_size - 1) / chunk_size;
    chunk_rows_ = (config_.height + chunk_size - 1) / chunk_size;
    chunks_.resize(static_cast<std::size_t>(chunk_columns_ * chunk_rows_));
}

std::int32_t World::width() const noexcept {
    return config_.width;
}

std::int32_t World::height() const noexcept {
    return config_.height;
}

Tick World::current_tick() const noexcept {
    return tick_;
}

std::uint64_t World::seed() const noexcept {
    return config_.seed;
}

bool World::in_bounds(Vec2i position) const noexcept {
    return position.x >= 0 && position.y >= 0 && position.x < config_.width &&
           position.y < config_.height;
}

const Cell& World::cell(Vec2i position) const {
    if (!in_bounds(position)) {
        throw std::out_of_range("cell position is outside the world");
    }
    return cell_unchecked(position);
}

MaterialId World::material(Vec2i position) const {
    return cell(position).material;
}

bool World::set_cell(Vec2i position, Cell value) {
    if (!in_bounds(position) || !is_valid(value.material)) {
        return false;
    }

    value.updated_epoch = 0;
    auto& destination = cell_unchecked(position);
    const bool equal_state =
        destination.material == value.material && destination.variant == value.variant &&
        destination.flags == value.flags && destination.temperature == value.temperature &&
        destination.velocity_x == value.velocity_x && destination.velocity_y == value.velocity_y;
    if (equal_state) {
        return false;
    }

    destination = value;
    mark_changed(position);
    return true;
}

bool World::set_material(Vec2i position, MaterialId material_id) {
    if (!in_bounds(position) || !is_valid(material_id)) {
        return false;
    }

    Cell value{};
    value.material = material_id;
    value.variant = static_cast<std::uint8_t>(noise(position, static_cast<std::uint8_t>(material_id)) &
                                              0x0FU);
    return set_cell(position, value);
}

std::size_t World::paint_disc(Vec2i center, std::int32_t radius, MaterialId material_id) {
    if (radius < 0 || !is_valid(material_id)) {
        return 0;
    }

    const auto radius_squared = static_cast<std::int64_t>(radius) * radius;
    std::size_t changed = 0;
    for (std::int32_t y = center.y - radius; y <= center.y + radius; ++y) {
        for (std::int32_t x = center.x - radius; x <= center.x + radius; ++x) {
            const auto dx = static_cast<std::int64_t>(x) - center.x;
            const auto dy = static_cast<std::int64_t>(y) - center.y;
            if (dx * dx + dy * dy <= radius_squared &&
                set_material({x, y}, material_id)) {
                ++changed;
            }
        }
    }
    return changed;
}

TickStats World::step() {
    ++tick_;
    const auto epoch = static_cast<std::uint8_t>(((tick_ - 1U) % 255U) + 1U);
    if (epoch == 1U && tick_ > 1U) {
        reset_update_epochs();
    }

    std::vector<std::uint8_t> active_at_start(chunks_.size(), 0);
    for (std::size_t index = 0; index < chunks_.size(); ++index) {
        active_at_start[index] = chunks_[index].active ? 1U : 0U;
    }

    TickStats stats{};
    stats.tick = tick_;

    for (std::int32_t y = config_.height - 1; y >= 0; --y) {
        const bool left_to_right = (noise({0, y}, tick_) & 1U) == 0U;
        for (std::int32_t offset = 0; offset < config_.width; ++offset) {
            const std::int32_t x = left_to_right ? offset : config_.width - 1 - offset;
            const Vec2i position{x, y};
            if (active_at_start[chunk_index(position)] != 0U) {
                update_cell(position, epoch, stats);
            }
        }
    }

    for (std::size_t index = 0; index < chunks_.size(); ++index) {
        auto& chunk = chunks_[index];
        if (chunk.changed) {
            ++chunk.revision;
            chunk.quiet_ticks = 0;
            chunk.active = true;
            chunk.changed = false;
            ++stats.changed_chunks;
        } else if (active_at_start[index] != 0U) {
            if (chunk.quiet_ticks < std::numeric_limits<std::uint16_t>::max()) {
                ++chunk.quiet_ticks;
            }
            if (chunk.quiet_ticks >= config_.sleep_after_ticks) {
                chunk.active = false;
            }
        }

        if (chunk.active) {
            ++stats.active_chunks;
        } else {
            ++stats.sleeping_chunks;
        }
    }

    return stats;
}

void World::wake_all() noexcept {
    for (auto& chunk : chunks_) {
        chunk.active = true;
        chunk.quiet_ticks = 0;
    }
}

void World::clear_dirty() noexcept {
    for (auto& chunk : chunks_) {
        chunk.dirty.clear();
    }
}

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
            hash_byte(hash, value.flags);
            hash_byte(hash, static_cast<std::uint8_t>(value.temperature));
            hash_byte(hash, static_cast<std::uint8_t>(value.temperature >> 8));
            hash_byte(hash, static_cast<std::uint8_t>(value.velocity_x));
            hash_byte(hash, static_cast<std::uint8_t>(value.velocity_y));
        }
    }
    return hash;
}

void World::rasterize_rgba(std::span<std::uint8_t> destination) const {
    const auto required =
        static_cast<std::size_t>(config_.width) * static_cast<std::size_t>(config_.height) * 4U;
    if (destination.size() < required) {
        throw std::invalid_argument("RGBA destination is smaller than the world");
    }

    std::size_t output = 0;
    for (std::int32_t y = 0; y < config_.height; ++y) {
        for (std::int32_t x = 0; x < config_.width; ++x) {
            const auto& value = cell_unchecked({x, y});
            auto color = material_definition(value.material).color;
            if (value.material != MaterialId::Empty) {
                const int variation = static_cast<int>(value.variant) - 8;
                color.r = shade(color.r, variation);
                color.g = shade(color.g, variation);
                color.b = shade(color.b, variation);
            }
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

std::size_t World::chunk_index(Vec2i position) const noexcept {
    const auto chunk_x = position.x / chunk_size;
    const auto chunk_y = position.y / chunk_size;
    return static_cast<std::size_t>(chunk_y * chunk_columns_ + chunk_x);
}

std::size_t World::local_index(Vec2i position) const noexcept {
    return Chunk::index(position.x % chunk_size, position.y % chunk_size);
}

Cell& World::cell_unchecked(Vec2i position) noexcept {
    return chunks_[chunk_index(position)].cells[local_index(position)];
}

const Cell& World::cell_unchecked(Vec2i position) const noexcept {
    return chunks_[chunk_index(position)].cells[local_index(position)];
}

void World::update_cell(Vec2i position, std::uint8_t epoch, TickStats& stats) {
    const auto value = cell_unchecked(position);
    if (value.updated_epoch == epoch || !is_dynamic(value.material)) {
        return;
    }

    const auto phase = material_definition(value.material).phase;
    const bool prefer_left = (noise(position, tick_ ^ 0xA5A5A5A5ULL) & 1U) == 0U;
    const std::int32_t first = prefer_left ? -1 : 1;
    const std::int32_t second = -first;

    if (phase == MaterialPhase::Granular) {
        if (try_move(position, {position.x, position.y + 1}, epoch, stats) ||
            try_move(position, {position.x + first, position.y + 1}, epoch, stats) ||
            try_move(position, {position.x + second, position.y + 1}, epoch, stats)) {
            return;
        }
    } else if (phase == MaterialPhase::Liquid) {
        if (try_move(position, {position.x, position.y + 1}, epoch, stats) ||
            try_move(position, {position.x + first, position.y + 1}, epoch, stats) ||
            try_move(position, {position.x + second, position.y + 1}, epoch, stats)) {
            return;
        }

        const auto dispersion =
            static_cast<std::int32_t>(material_definition(value.material).dispersion);
        for (const auto direction : std::array<std::int32_t, 2>{first, second}) {
            Vec2i destination = position;
            for (std::int32_t distance = 1; distance <= dispersion; ++distance) {
                const Vec2i candidate{position.x + direction * distance, position.y};
                if (!in_bounds(candidate) ||
                    material_definition(cell_unchecked(candidate).material).phase !=
                        MaterialPhase::Empty) {
                    break;
                }
                destination = candidate;
            }
            if (destination != position && try_move(position, destination, epoch, stats)) {
                return;
            }
        }
    }
}

bool World::try_move(Vec2i from, Vec2i to, std::uint8_t epoch, TickStats& stats) {
    if (!in_bounds(to)) {
        return false;
    }

    const auto source = cell_unchecked(from);
    const auto target = cell_unchecked(to);
    const auto& source_definition = material_definition(source.material);
    const auto& target_definition = material_definition(target.material);

    const bool into_empty = target_definition.phase == MaterialPhase::Empty;
    const bool sinks_through_liquid =
        source_definition.phase == MaterialPhase::Granular &&
        target_definition.phase == MaterialPhase::Liquid &&
        source_definition.density > target_definition.density;
    if (!into_empty && !sinks_through_liquid) {
        return false;
    }

    auto moved_source = source;
    auto displaced_target = target;
    moved_source.updated_epoch = epoch;
    displaced_target.updated_epoch = epoch;
    cell_unchecked(to) = moved_source;
    cell_unchecked(from) = displaced_target;

    mark_changed(from);
    mark_changed(to);
    ++stats.moved_cells;
    return true;
}

void World::mark_changed(Vec2i position) noexcept {
    auto& chunk = chunks_[chunk_index(position)];
    chunk.changed = true;
    chunk.active = true;
    chunk.quiet_ticks = 0;
    chunk.dirty.include(position.x % chunk_size, position.y % chunk_size);
    wake_neighborhood(position);
}

void World::wake_neighborhood(Vec2i position) noexcept {
    const auto center_x = position.x / chunk_size;
    const auto center_y = position.y / chunk_size;
    for (std::int32_t chunk_y = center_y - 1; chunk_y <= center_y + 1; ++chunk_y) {
        for (std::int32_t chunk_x = center_x - 1; chunk_x <= center_x + 1; ++chunk_x) {
            if (chunk_x < 0 || chunk_y < 0 || chunk_x >= chunk_columns_ ||
                chunk_y >= chunk_rows_) {
                continue;
            }
            auto& chunk =
                chunks_[static_cast<std::size_t>(chunk_y * chunk_columns_ + chunk_x)];
            chunk.active = true;
            chunk.quiet_ticks = 0;
        }
    }
}

void World::reset_update_epochs() noexcept {
    for (auto& chunk : chunks_) {
        for (auto& value : chunk.cells) {
            value.updated_epoch = 0;
        }
    }
}

std::uint64_t World::noise(Vec2i position, std::uint64_t salt) const noexcept {
    std::uint64_t value = config_.seed ^ salt;
    value ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(position.x)) *
             0x9E3779B185EBCA87ULL;
    value ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(position.y)) *
             0xC2B2AE3D27D4EB4FULL;
    value ^= value >> 30U;
    value *= 0xBF58476D1CE4E5B9ULL;
    value ^= value >> 27U;
    value *= 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

} // namespace meat2d
