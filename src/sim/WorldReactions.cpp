#include "meat2d/sim/World.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace meat2d {
namespace {

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

} // namespace

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

} // namespace meat2d
