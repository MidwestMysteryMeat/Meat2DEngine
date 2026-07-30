#include "meat2d/sim/World.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
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
constexpr std::array<Vec2i, 8> neighbor_directions{{
    {-1, -1},
    {0, -1},
    {1, -1},
    {-1, 0},
    {1, 0},
    {-1, 1},
    {0, 1},
    {1, 1},
}};

constexpr std::int16_t fixed_celsius(int value) noexcept {
    return static_cast<std::int16_t>(value * 16);
}

std::int16_t clamp_temperature(int value) noexcept {
    return static_cast<std::int16_t>(std::clamp(
        value,
        static_cast<int>(std::numeric_limits<std::int16_t>::min()),
        static_cast<int>(std::numeric_limits<std::int16_t>::max())));
}

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
        destination.state == value.state && destination.temperature == value.temperature &&
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
    value.state = initial_state(material_id);
    value.temperature = material_definition(material_id).default_temperature;
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

RaycastHit World::raycast(Vec2i origin, Vec2i target) const noexcept {
    RaycastHit hit{target, MaterialId::Empty, false};
    if (!in_bounds(origin) || !in_bounds(target)) {
        return hit;
    }

    auto x = origin.x;
    auto y = origin.y;
    const auto delta_x = std::abs(target.x - origin.x);
    const auto delta_y = -std::abs(target.y - origin.y);
    const auto step_x = origin.x < target.x ? 1 : -1;
    const auto step_y = origin.y < target.y ? 1 : -1;
    auto error = delta_x + delta_y;

    while (true) {
        const bool at_origin = x == origin.x && y == origin.y;
        const bool at_target = x == target.x && y == target.y;
        if (!at_origin && !at_target) {
            const auto found = material({x, y});
            if (blocks_line_of_sight(found)) {
                return {{x, y}, found, true};
            }
        }
        if (at_target) {
            break;
        }
        const auto doubled_error = 2 * error;
        if (doubled_error >= delta_y) {
            error += delta_y;
            x += step_x;
        }
        if (doubled_error <= delta_x) {
            error += delta_x;
            y += step_y;
        }
    }

    hit.material = material(target);
    return hit;
}

bool World::line_of_sight(Vec2i origin, Vec2i target) const noexcept {
    return !raycast(origin, target).blocked;
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
    auto value = cell_unchecked(position);
    if (value.updated_epoch == epoch || value.material == MaterialId::Empty) {
        return;
    }

    exchange_heat(position, stats);
    if (apply_phase_change(position, epoch, stats)) {
        return;
    }

    value = cell_unchecked(position);
    const auto& definition = material_definition(value.material);
    if (has_flag(value.material, MaterialFlags::Flammable) &&
        value.temperature >= definition.ignition_temperature) {
        if (value.material == MaterialId::Gunpowder) {
            explode(position, 5, epoch, stats);
        } else if (value.material == MaterialId::ExplosiveGas) {
            explode(position, 7, epoch, stats);
        } else {
            transform_cell(position, MaterialId::Fire, epoch, stats, true);
        }
        return;
    }

    switch (value.material) {
    case MaterialId::Fire:
        update_fire(position, epoch, stats);
        break;
    case MaterialId::Smoke: {
        auto& smoke = cell_unchecked(position);
        if (smoke.state <= 1U) {
            transform_cell(position, MaterialId::Empty, epoch, stats, false);
            return;
        }
        --smoke.state;
        mark_changed(position);
        break;
    }
    case MaterialId::Acid:
        update_acid(position, epoch, stats);
        break;
    case MaterialId::Lava:
        update_lava(position, epoch, stats);
        break;
    case MaterialId::Plant:
    case MaterialId::Seed:
        update_plant(position, epoch, stats);
        break;
    case MaterialId::Soil:
        if (has_neighbor(position, MaterialId::Water) &&
            (noise(position, tick_ ^ 0x534F494CULL) & 15U) == 0U) {
            transform_cell(position, MaterialId::Mud, epoch, stats, true);
            return;
        }
        break;
    case MaterialId::Salt:
        if (has_neighbor(position, MaterialId::Water)) {
            transform_cell(position, MaterialId::Empty, epoch, stats, false);
            return;
        }
        break;
    case MaterialId::Electricity:
        update_electricity(position, epoch, stats);
        return;
    case MaterialId::Metal:
        if (value.state > 0U) {
            update_charged_metal(position, epoch);
        }
        return;
    default:
        break;
    }

    value = cell_unchecked(position);
    if (value.updated_epoch == epoch || value.material == MaterialId::Empty) {
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
    } else if (phase == MaterialPhase::Gas) {
        if (try_move(position, {position.x, position.y - 1}, epoch, stats) ||
            try_move(position, {position.x + first, position.y - 1}, epoch, stats) ||
            try_move(position, {position.x + second, position.y - 1}, epoch, stats)) {
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
    const bool target_is_displaceable =
        target_definition.phase == MaterialPhase::Liquid ||
        target_definition.phase == MaterialPhase::Gas;
    const bool sinks_through_lighter_material =
        to.y >= from.y && target_is_displaceable &&
        source_definition.density > target_definition.density;
    const bool rises_through_heavier_material =
        to.y <= from.y && source_definition.phase == MaterialPhase::Gas &&
        target_is_displaceable && source_definition.density < target_definition.density;
    if (!into_empty && !sinks_through_lighter_material &&
        !rises_through_heavier_material) {
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

bool World::transform_cell(
    Vec2i position,
    MaterialId material_id,
    std::uint8_t epoch,
    TickStats& stats,
    bool preserve_temperature) {
    if (!in_bounds(position) || !is_valid(material_id)) {
        return false;
    }

    const auto previous = cell_unchecked(position);
    if (previous.material == material_id) {
        return false;
    }

    Cell replacement{};
    replacement.material = material_id;
    replacement.variant = static_cast<std::uint8_t>(
        noise(position, tick_ ^ static_cast<std::uint8_t>(material_id)) & 0x0FU);
    replacement.updated_epoch = epoch;
    replacement.state = initial_state(material_id);
    replacement.temperature =
        preserve_temperature ? previous.temperature
                             : material_definition(material_id).default_temperature;
    cell_unchecked(position) = replacement;
    mark_changed(position);
    ++stats.reacted_cells;
    return true;
}

bool World::apply_phase_change(Vec2i position, std::uint8_t epoch, TickStats& stats) {
    const auto value = cell_unchecked(position);
    switch (value.material) {
    case MaterialId::Water:
        if (value.temperature <= fixed_celsius(0)) {
            return transform_cell(position, MaterialId::Ice, epoch, stats, true);
        }
        if (value.temperature >= fixed_celsius(100)) {
            return transform_cell(position, MaterialId::Steam, epoch, stats, true);
        }
        break;
    case MaterialId::Steam:
        if (value.temperature <= fixed_celsius(90)) {
            return transform_cell(position, MaterialId::Water, epoch, stats, true);
        }
        break;
    case MaterialId::Snow:
        if (value.temperature >= fixed_celsius(1)) {
            return transform_cell(position, MaterialId::Water, epoch, stats, true);
        }
        break;
    case MaterialId::Ice:
        if (value.temperature >= fixed_celsius(2)) {
            return transform_cell(position, MaterialId::Water, epoch, stats, true);
        }
        break;
    case MaterialId::Lava:
        if (value.temperature <= fixed_celsius(700)) {
            return transform_cell(position, MaterialId::Obsidian, epoch, stats, true);
        }
        break;
    case MaterialId::Mud:
        if (value.temperature >= fixed_celsius(100)) {
            return transform_cell(position, MaterialId::Soil, epoch, stats, true);
        }
        break;
    default:
        break;
    }
    return false;
}

void World::exchange_heat(Vec2i position, TickStats& stats) {
    const auto source = cell_unchecked(position);
    if (source.material == MaterialId::Empty) {
        return;
    }

    const auto direction_index =
        static_cast<std::size_t>(noise(position, tick_ ^ 0x48454154ULL) & 3U);
    const Vec2i direction = cardinal_directions[direction_index];
    const Vec2i neighbor{position.x + direction.x, position.y + direction.y};
    if (!in_bounds(neighbor)) {
        return;
    }

    const auto target = cell_unchecked(neighbor);
    if (target.material == MaterialId::Empty) {
        return;
    }

    const auto conductivity = static_cast<int>(std::min(
        material_definition(source.material).thermal_conductivity,
        material_definition(target.material).thermal_conductivity));
    const int difference =
        static_cast<int>(target.temperature) - static_cast<int>(source.temperature);
    if (conductivity == 0 || difference == 0) {
        return;
    }

    int transfer = difference * conductivity / 1024;
    if (transfer == 0) {
        transfer = difference > 0 ? 1 : -1;
    }
    const int half_difference = difference / 2;
    if (difference > 0) {
        transfer = std::min(transfer, half_difference);
    } else {
        transfer = std::max(transfer, half_difference);
    }
    if (transfer == 0) {
        return;
    }

    auto& mutable_source = cell_unchecked(position);
    auto& mutable_target = cell_unchecked(neighbor);
    mutable_source.temperature =
        clamp_temperature(static_cast<int>(mutable_source.temperature) + transfer);
    mutable_target.temperature =
        clamp_temperature(static_cast<int>(mutable_target.temperature) - transfer);
    mark_changed(position);
    mark_changed(neighbor);
    ++stats.heat_transfers;
}

void World::update_fire(Vec2i position, std::uint8_t epoch, TickStats& stats) {
    auto& fire = cell_unchecked(position);
    if (fire.material != MaterialId::Fire) {
        return;
    }
    if (fire.state <= 1U) {
        const auto residue = (noise(position, tick_ ^ 0x534D4F4B45ULL) & 3U) == 0U
                                 ? MaterialId::Empty
                                 : MaterialId::Smoke;
        transform_cell(position, residue, epoch, stats, false);
        return;
    }

    --fire.state;
    fire.temperature = std::max(fire.temperature, fixed_celsius(600));
    mark_changed(position);

    for (const auto direction : neighbor_directions) {
        const Vec2i neighbor{position.x + direction.x, position.y + direction.y};
        if (!in_bounds(neighbor)) {
            continue;
        }

        auto& target = cell_unchecked(neighbor);
        if (target.material == MaterialId::Empty || target.material == MaterialId::Fire) {
            continue;
        }

        target.temperature =
            clamp_temperature(static_cast<int>(target.temperature) + fixed_celsius(55));
        mark_changed(neighbor);

        if (target.material == MaterialId::Gunpowder) {
            explode(neighbor, 5, epoch, stats);
            return;
        }
        if (target.material == MaterialId::ExplosiveGas) {
            explode(neighbor, 7, epoch, stats);
            return;
        }

        const auto target_material = target.material;
        if (has_flag(target_material, MaterialFlags::Flammable) &&
            target.temperature >= material_definition(target_material).ignition_temperature) {
            transform_cell(neighbor, MaterialId::Fire, epoch, stats, true);
        }
    }
}

void World::update_acid(Vec2i position, std::uint8_t epoch, TickStats& stats) {
    auto& acid = cell_unchecked(position);
    if (acid.material != MaterialId::Acid) {
        return;
    }
    if (acid.state == 0U) {
        transform_cell(position, MaterialId::Water, epoch, stats, true);
        return;
    }

    const auto start =
        static_cast<std::size_t>(noise(position, tick_ ^ 0x41434944ULL) & 7U);
    for (std::size_t offset = 0; offset < neighbor_directions.size(); ++offset) {
        const auto direction =
            neighbor_directions[(start + offset) % neighbor_directions.size()];
        const Vec2i neighbor{position.x + direction.x, position.y + direction.y};
        if (!in_bounds(neighbor)) {
            continue;
        }

        const auto target_material = cell_unchecked(neighbor).material;
        if (!has_flag(target_material, MaterialFlags::Corrodible)) {
            continue;
        }

        transform_cell(neighbor, MaterialId::Empty, epoch, stats, false);
        auto& remaining_acid = cell_unchecked(position);
        remaining_acid.state =
            remaining_acid.state > 24U ? static_cast<std::uint8_t>(remaining_acid.state - 24U)
                                       : 0U;
        remaining_acid.updated_epoch = epoch;
        mark_changed(position);
        if (remaining_acid.state == 0U) {
            transform_cell(position, MaterialId::Water, epoch, stats, true);
        }
        return;
    }
}

void World::update_lava(Vec2i position, std::uint8_t epoch, TickStats& stats) {
    auto& lava = cell_unchecked(position);
    if (lava.material != MaterialId::Lava) {
        return;
    }

    for (const auto direction : neighbor_directions) {
        const Vec2i neighbor{position.x + direction.x, position.y + direction.y};
        if (!in_bounds(neighbor)) {
            continue;
        }

        const auto target_material = cell_unchecked(neighbor).material;
        if (target_material == MaterialId::Water || target_material == MaterialId::Snow ||
            target_material == MaterialId::Ice) {
            transform_cell(neighbor, MaterialId::Steam, epoch, stats, true);
            transform_cell(position, MaterialId::Obsidian, epoch, stats, true);
            return;
        }

        auto& target = cell_unchecked(neighbor);
        if (has_flag(target_material, MaterialFlags::Flammable)) {
            target.temperature =
                clamp_temperature(static_cast<int>(target.temperature) + fixed_celsius(90));
            mark_changed(neighbor);
            if (target.temperature >=
                material_definition(target_material).ignition_temperature) {
                if (target_material == MaterialId::Gunpowder) {
                    explode(neighbor, 5, epoch, stats);
                } else if (target_material == MaterialId::ExplosiveGas) {
                    explode(neighbor, 7, epoch, stats);
                } else {
                    transform_cell(neighbor, MaterialId::Fire, epoch, stats, true);
                }
                return;
            }
        }
    }

    auto& cooling_lava = cell_unchecked(position);
    cooling_lava.temperature =
        clamp_temperature(static_cast<int>(cooling_lava.temperature) - fixed_celsius(1));
    mark_changed(position);
    if (cooling_lava.temperature <= fixed_celsius(700)) {
        transform_cell(position, MaterialId::Obsidian, epoch, stats, true);
    }
}

void World::update_plant(Vec2i position, std::uint8_t epoch, TickStats& stats) {
    const auto material_id = cell_unchecked(position).material;
    if (material_id == MaterialId::Seed) {
        const Vec2i below{position.x, position.y + 1};
        if (!in_bounds(below)) {
            return;
        }
        const auto support = cell_unchecked(below).material;
        const bool fertile =
            support == MaterialId::Soil || support == MaterialId::Mud ||
            support == MaterialId::Plant;
        if (fertile && has_neighbor(position, MaterialId::Water) &&
            (noise(position, tick_ ^ 0x53454544ULL) & 15U) == 0U) {
            transform_cell(position, MaterialId::Plant, epoch, stats, true);
        }
        return;
    }

    if (material_id != MaterialId::Plant || !has_neighbor(position, MaterialId::Water) ||
        (noise(position, tick_ ^ 0x504C414E54ULL) & 63U) != 0U) {
        return;
    }

    const bool prefer_left = (noise(position, tick_ ^ 0x47524F57ULL) & 1U) == 0U;
    const std::array<Vec2i, 3> growth_directions{{
        {0, -1},
        {prefer_left ? -1 : 1, -1},
        {prefer_left ? 1 : -1, -1},
    }};
    for (const auto direction : growth_directions) {
        const Vec2i destination{position.x + direction.x, position.y + direction.y};
        if (in_bounds(destination) &&
            cell_unchecked(destination).material == MaterialId::Empty) {
            transform_cell(destination, MaterialId::Plant, epoch, stats, true);
            return;
        }
    }
}

void World::update_electricity(Vec2i position, std::uint8_t epoch, TickStats& stats) {
    for (const auto direction : neighbor_directions) {
        const Vec2i neighbor{position.x + direction.x, position.y + direction.y};
        if (!in_bounds(neighbor)) {
            continue;
        }

        auto& target = cell_unchecked(neighbor);
        if (has_flag(target.material, MaterialFlags::Conductive)) {
            target.state = std::max(target.state, initial_state(MaterialId::Electricity));
            target.temperature =
                clamp_temperature(static_cast<int>(target.temperature) + fixed_celsius(8));
            target.updated_epoch = epoch;
            mark_changed(neighbor);
        } else if (has_flag(target.material, MaterialFlags::Flammable)) {
            target.temperature =
                clamp_temperature(static_cast<int>(target.temperature) + fixed_celsius(220));
            mark_changed(neighbor);
            if (target.temperature >=
                material_definition(target.material).ignition_temperature) {
                transform_cell(neighbor, MaterialId::Fire, epoch, stats, true);
            }
        }
    }
    transform_cell(position, MaterialId::Empty, epoch, stats, false);
}

void World::update_charged_metal(Vec2i position, std::uint8_t epoch) {
    auto& metal = cell_unchecked(position);
    if (metal.material != MaterialId::Metal || metal.state == 0U) {
        return;
    }

    const auto charge = metal.state;
    metal.state = static_cast<std::uint8_t>(charge - 1U);
    metal.updated_epoch = epoch;
    mark_changed(position);

    if (charge <= 1U) {
        return;
    }

    const auto start =
        static_cast<std::size_t>(noise(position, tick_ ^ 0x434841524745ULL) & 7U);
    for (std::size_t offset = 0; offset < neighbor_directions.size(); ++offset) {
        const auto direction =
            neighbor_directions[(start + offset) % neighbor_directions.size()];
        const Vec2i neighbor{position.x + direction.x, position.y + direction.y};
        if (!in_bounds(neighbor)) {
            continue;
        }

        auto& target = cell_unchecked(neighbor);
        if (!has_flag(target.material, MaterialFlags::Conductive) ||
            target.state >= charge - 1U) {
            continue;
        }
        target.state = static_cast<std::uint8_t>(charge - 1U);
        target.updated_epoch = epoch;
        mark_changed(neighbor);
        break;
    }
}

void World::explode(
    Vec2i center,
    std::int32_t radius,
    std::uint8_t epoch,
    TickStats& stats) {
    const auto radius_squared = static_cast<std::int64_t>(radius) * radius;
    for (std::int32_t y = center.y - radius; y <= center.y + radius; ++y) {
        for (std::int32_t x = center.x - radius; x <= center.x + radius; ++x) {
            const Vec2i position{x, y};
            if (!in_bounds(position)) {
                continue;
            }

            const auto dx = static_cast<std::int64_t>(x) - center.x;
            const auto dy = static_cast<std::int64_t>(y) - center.y;
            const auto distance_squared = dx * dx + dy * dy;
            if (distance_squared > radius_squared) {
                continue;
            }

            const int force = 255 - static_cast<int>(
                                        distance_squared * 220 /
                                        std::max<std::int64_t>(radius_squared, 1));
            auto& target = cell_unchecked(position);
            const auto target_material = target.material;
            if (target_material == MaterialId::Empty) {
                if ((noise(position, tick_ ^ 0x424F4F4DULL) & 3U) == 0U) {
                    transform_cell(position, MaterialId::Fire, epoch, stats, false);
                }
                continue;
            }

            target.temperature =
                clamp_temperature(static_cast<int>(target.temperature) + fixed_celsius(160));
            mark_changed(position);

            const auto& definition = material_definition(target_material);
            if (force <= definition.blast_resistance) {
                continue;
            }
            if (target_material == MaterialId::Fire) {
                continue;
            }
            if (has_flag(target_material, MaterialFlags::Flammable) ||
                target_material == MaterialId::Gunpowder ||
                target_material == MaterialId::ExplosiveGas) {
                transform_cell(position, MaterialId::Fire, epoch, stats, true);
            } else if (definition.phase == MaterialPhase::StaticSolid &&
                       has_flag(target_material, MaterialFlags::Destructible)) {
                const auto residue =
                    force > static_cast<int>(definition.blast_resistance) + 80
                        ? MaterialId::Empty
                        : MaterialId::Debris;
                transform_cell(position, residue, epoch, stats, true);
            } else if (definition.phase != MaterialPhase::StaticSolid) {
                transform_cell(position, MaterialId::Empty, epoch, stats, false);
            }
        }
    }
}

bool World::has_neighbor(Vec2i position, MaterialId material_id) const noexcept {
    for (const auto direction : neighbor_directions) {
        const Vec2i neighbor{position.x + direction.x, position.y + direction.y};
        if (in_bounds(neighbor) && cell_unchecked(neighbor).material == material_id) {
            return true;
        }
    }
    return false;
}

std::uint8_t World::initial_state(MaterialId material_id) const noexcept {
    switch (material_id) {
    case MaterialId::Fire:
        return 180U;
    case MaterialId::Smoke:
        return 120U;
    case MaterialId::Acid:
        return 168U;
    case MaterialId::Electricity:
        return 12U;
    default:
        return 0U;
    }
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
