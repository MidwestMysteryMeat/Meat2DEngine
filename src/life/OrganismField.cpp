#include "meat2d/life/OrganismField.hpp"

#include "meat2d/sim/World.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace meat2d::life {
namespace {

constexpr std::uint64_t fnv_offset = 14695981039346656037ULL;
constexpr std::uint64_t fnv_prime = 1099511628211ULL;
constexpr std::array<Vec2i, 8> directions{{
    {0, -1},
    {1, -1},
    {1, 0},
    {1, 1},
    {0, 1},
    {-1, 1},
    {-1, 0},
    {-1, -1},
}};

std::int32_t absolute(std::int32_t value) noexcept {
    return value < 0 ? -value : value;
}

void hash_byte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= value;
    hash *= fnv_prime;
}

template <typename Integer>
void hash_integer(std::uint64_t& hash, Integer value) noexcept {
    using Unsigned = std::make_unsigned_t<Integer>;
    const auto converted = static_cast<Unsigned>(value);
    for (std::size_t offset = 0; offset < sizeof(Integer); ++offset) {
        hash_byte(hash, static_cast<std::uint8_t>(converted >> (offset * 8U)));
    }
}

} // namespace

OrganismField::OrganismField(
    std::int32_t width,
    std::int32_t height,
    std::uint64_t seed)
    : width_(width), height_(height), seed_(seed) {
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument("organism field dimensions must be positive");
    }
    const auto count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    cells_.resize(count);
    next_.resize(count);
}

std::int32_t OrganismField::width() const noexcept {
    return width_;
}

std::int32_t OrganismField::height() const noexcept {
    return height_;
}

std::uint32_t OrganismField::population() const noexcept {
    return population_;
}

bool OrganismField::in_bounds(Vec2i position) const noexcept {
    return position.x >= 0 && position.y >= 0 && position.x < width_ &&
           position.y < height_;
}

const OrganismCell& OrganismField::cell(Vec2i position) const {
    if (!in_bounds(position)) {
        throw std::out_of_range("organism position is outside the field");
    }
    return cells_[index(position)];
}

std::span<const OrganismCell> OrganismField::cells() const noexcept {
    return cells_;
}

bool OrganismField::seed(Vec2i position, std::uint32_t genome, std::uint16_t energy) {
    if (!in_bounds(position) || genome == 0U || energy == 0U ||
        cells_[index(position)].genome != 0U) {
        return false;
    }
    cells_[index(position)] = {
        .genome = genome,
        .energy = energy,
        .age = 0,
    };
    ++population_;
    return true;
}

bool OrganismField::erase(Vec2i position) {
    if (!in_bounds(position) || cells_[index(position)].genome == 0U) {
        return false;
    }
    cells_[index(position)] = {};
    --population_;
    return true;
}

OrganismStats OrganismField::step(World& world) {
    if (world.width() != width_ || world.height() != height_) {
        throw std::invalid_argument("organism field and world dimensions must match");
    }

    std::fill(next_.begin(), next_.end(), OrganismCell{});
    std::vector<Vec2i> consumed;
    consumed.reserve(64);
    OrganismStats stats{};
    const auto tick = world.current_tick();
    const bool reverse_y = (tick & 1U) != 0U;
    const bool reverse_x = (tick & 2U) != 0U;

    for (std::int32_t y_offset = 0; y_offset < height_; ++y_offset) {
        const auto y = reverse_y ? height_ - 1 - y_offset : y_offset;
        for (std::int32_t x_offset = 0; x_offset < width_; ++x_offset) {
            const auto x = reverse_x ? width_ - 1 - x_offset : x_offset;
            const Vec2i position{x, y};
            auto organism = cells_[index(position)];
            if (organism.genome == 0U) {
                continue;
            }
            ++stats.evaluated;

            const auto traits = decode_traits(organism.genome);
            organism.age = organism.age < std::numeric_limits<std::uint16_t>::max()
                               ? static_cast<std::uint16_t>(organism.age + 1U)
                               : organism.age;

            int energy = static_cast<int>(organism.energy) - 2 -
                         static_cast<int>(traits.motility) / 5;
            const auto material_id = world.material(position);
            const auto temperature =
                static_cast<int>(world.cell(position).temperature) / 16;
            const auto preferred_temperature =
                -10 + static_cast<int>(traits.heat_preference) * 8;
            const auto temperature_error =
                absolute(temperature - preferred_temperature);
            const auto tolerance = 22 + static_cast<int>(traits.resilience) * 3;
            if (temperature_error > tolerance) {
                energy -= 1 + (temperature_error - tolerance) / 6;
            }

            if (material_id == MaterialId::Fire || material_id == MaterialId::Lava) {
                energy -= std::max(20, 90 - static_cast<int>(traits.resilience) * 4);
            } else if (material_id == MaterialId::Acid) {
                energy -= std::max(2, 38 - static_cast<int>(traits.resilience) * 2);
            }

            if (material_id == MaterialId::Plant && traits.digestion > 0U) {
                energy += 20 + static_cast<int>(traits.digestion) * 7;
                consumed.push_back(position);
            }
            if (traits.photosynthesis > 0U && exposed_to_air(world, position)) {
                energy += static_cast<int>(traits.photosynthesis) / 2;
                if (has_water(world, position)) {
                    energy += 2 + static_cast<int>(traits.photosynthesis) / 3;
                }
            }

            if (organism.age > 8'000U &&
                noise(position, tick, 0x414745ULL) % 2'048U == 0U) {
                energy = 0;
            }
            if (energy <= 0) {
                ++stats.deaths;
                continue;
            }
            organism.energy = static_cast<std::uint16_t>(
                std::min(2'000, energy));

            const auto destination =
                choose_destination(world, position, organism, traits);
            const auto destination_index = index(destination);
            const bool destination_available =
                destination != position && cells_[destination_index].genome == 0U &&
                next_[destination_index].genome == 0U;
            const bool wants_reproduction =
                organism.energy >
                    static_cast<std::uint16_t>(
                        760 - static_cast<int>(traits.reproduction) * 22) &&
                noise(position, tick, 0x524550524FULL) % 16U <
                    std::max<std::uint8_t>(1U, traits.reproduction);
            const bool wants_movement =
                traits.motility > 0U &&
                noise(position, tick, 0x4D4F5645ULL) % 20U < traits.motility;

            if (wants_reproduction && destination_available) {
                auto child = organism;
                child.age = 0;
                child.energy = static_cast<std::uint16_t>(organism.energy / 2U);
                organism.energy =
                    static_cast<std::uint16_t>(organism.energy - child.energy);

                const auto mutation_denominator =
                    static_cast<std::uint64_t>(
                        std::max(48, 512 - static_cast<int>(traits.mutation) * 28));
                if (noise(position, tick, 0x4D5554415445ULL) %
                        mutation_denominator ==
                    0U) {
                    const auto bit = static_cast<std::uint32_t>(
                        noise(position, tick, 0x424954ULL) & 31U);
                    child.genome ^= 1U << bit;
                    if (child.genome == 0U) {
                        child.genome = 1U;
                    }
                    ++stats.mutations;
                }
                next_[destination_index] = child;
                ++stats.births;
            } else if (wants_movement && destination_available) {
                next_[destination_index] = organism;
                ++stats.moves;
                continue;
            }

            const auto source_index = index(position);
            if (next_[source_index].genome == 0U) {
                next_[source_index] = organism;
            } else {
                ++stats.deaths;
            }
        }
    }

    for (const auto position : consumed) {
        if (world.material(position) == MaterialId::Plant &&
            world.set_material(position, MaterialId::Empty)) {
            ++stats.consumed_cells;
        }
    }

    cells_.swap(next_);
    population_ = 0;
    for (const auto& organism : cells_) {
        if (organism.genome != 0U) {
            ++population_;
        }
    }
    stats.population = population_;
    return stats;
}

Rgba8 OrganismField::color(const OrganismCell& organism) const noexcept {
    if (organism.genome == 0U) {
        return {0, 0, 0, 0};
    }
    const auto traits = decode_traits(organism.genome);
    const auto pigment = static_cast<int>(traits.pigment);
    const auto energy =
        std::min(80, static_cast<int>(organism.energy) / 20);
    return {
        static_cast<std::uint8_t>(70 + (pigment * 73) % 130 + energy / 4),
        static_cast<std::uint8_t>(80 + (pigment * 47) % 120 + energy / 3),
        static_cast<std::uint8_t>(75 + (pigment * 29) % 125 + energy / 5),
        230,
    };
}

std::uint64_t OrganismField::state_hash() const noexcept {
    std::uint64_t hash = fnv_offset;
    hash_integer(hash, width_);
    hash_integer(hash, height_);
    hash_integer(hash, seed_);
    for (const auto& organism : cells_) {
        hash_integer(hash, organism.genome);
        hash_integer(hash, organism.energy);
        hash_integer(hash, organism.age);
    }
    return hash;
}

std::size_t OrganismField::index(Vec2i position) const noexcept {
    return static_cast<std::size_t>(position.y) * static_cast<std::size_t>(width_) +
           static_cast<std::size_t>(position.x);
}

Vec2i OrganismField::choose_destination(
    const World& world,
    Vec2i position,
    const OrganismCell& organism,
    OrganismTraits traits) const {
    auto best = position;
    auto best_score = std::numeric_limits<int>::min();
    const auto start =
        static_cast<std::size_t>(
            noise(position, world.current_tick(), organism.genome) % directions.size());
    for (std::size_t offset = 0; offset < directions.size(); ++offset) {
        const auto direction = directions[(start + offset) % directions.size()];
        const Vec2i candidate{position.x + direction.x, position.y + direction.y};
        if (!in_bounds(candidate) || cells_[index(candidate)].genome != 0U) {
            continue;
        }

        const auto material_id = world.material(candidate);
        const auto candidate_temperature =
            static_cast<int>(world.cell(candidate).temperature) / 16;
        const auto preferred_temperature =
            -10 + static_cast<int>(traits.heat_preference) * 8;
        int score =
            30 - absolute(candidate_temperature - preferred_temperature);
        if (material_id == MaterialId::Plant) {
            score += static_cast<int>(traits.digestion) * 9;
        } else if (material_id == MaterialId::Water) {
            score += 12;
        } else if (material_id == MaterialId::Soil ||
                   material_id == MaterialId::Mud) {
            score += 5;
        } else if (material_id == MaterialId::Fire ||
                   material_id == MaterialId::Lava) {
            score -= 180 - static_cast<int>(traits.resilience) * 6;
        } else if (material_id == MaterialId::Acid) {
            score -= 80 - static_cast<int>(traits.resilience) * 5;
        }
        if (score > best_score) {
            best = candidate;
            best_score = score;
        }
    }
    return best;
}

bool OrganismField::exposed_to_air(const World& world, Vec2i position) const noexcept {
    const Vec2i above{position.x, position.y - 1};
    if (!in_bounds(above)) {
        return true;
    }
    const auto phase = material_definition(world.material(above)).phase;
    return phase == MaterialPhase::Empty || phase == MaterialPhase::Gas;
}

bool OrganismField::has_water(const World& world, Vec2i position) const noexcept {
    for (const auto direction : directions) {
        const Vec2i candidate{position.x + direction.x, position.y + direction.y};
        if (in_bounds(candidate) && world.material(candidate) == MaterialId::Water) {
            return true;
        }
    }
    return false;
}

std::uint64_t OrganismField::noise(
    Vec2i position,
    Tick tick,
    std::uint64_t salt) const noexcept {
    std::uint64_t value = seed_ ^ tick ^ salt;
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

} // namespace meat2d::life
