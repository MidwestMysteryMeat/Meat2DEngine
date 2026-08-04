#include "meat2d/sim/World.hpp"

#include <cstdlib>
#include <stdexcept>

namespace meat2d {

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
            if (dx * dx + dy * dy <= radius_squared && set_material({x, y}, material_id)) {
                ++changed;
            }
        }
    }
    return changed;
}

RaycastHit World::raycast(Vec2i origin, Vec2i target) const noexcept {
    RaycastHit hit{target, MaterialId::Empty, false};
    if (!in_bounds(origin) || !in_bounds(target)) {
        // Conservative: a ray with an out-of-bounds endpoint must never
        // report a clear line, or callers (projectiles, line_of_sight)
        // would treat unmapped space as open and tunnel through walls.
        hit.position = in_bounds(origin) ? target : origin;
        hit.blocked = true;
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

} // namespace meat2d
