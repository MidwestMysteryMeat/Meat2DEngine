#pragma once

#include "meat2d/core/Types.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace meat2d {

// Material IDs are serialized and replicated. Existing values must never be
// reordered; append new materials before Count.
enum class MaterialId : std::uint8_t {
    Empty = 0,
    Sand = 1,
    Water = 2,
    Stone = 3,
    Wood = 4,
    Oil = 5,
    Fire = 6,
    Smoke = 7,
    Steam = 8,
    Metal = 9,
    Acid = 10,
    Plant = 11,
    Seed = 12,
    Soil = 13,
    Mud = 14,
    Salt = 15,
    Snow = 16,
    Ice = 17,
    Lava = 18,
    Obsidian = 19,
    Gunpowder = 20,
    ExplosiveGas = 21,
    Concrete = 22,
    Electricity = 23,
    Debris = 24,
    Count
};

enum class MaterialPhase : std::uint8_t {
    Empty,
    Granular,
    Liquid,
    Gas,
    StaticSolid,
    Energy
};

enum class MaterialFlags : std::uint16_t {
    None = 0,
    Flammable = 1U << 0U,
    Corrodible = 1U << 1U,
    Conductive = 1U << 2U,
    Organic = 1U << 3U,
    Destructible = 1U << 4U
};

[[nodiscard]] constexpr MaterialFlags operator|(MaterialFlags left, MaterialFlags right) noexcept {
    return static_cast<MaterialFlags>(
        static_cast<std::uint16_t>(left) | static_cast<std::uint16_t>(right));
}

struct MaterialDefinition {
    std::string_view name;
    Rgba8 color;
    MaterialPhase phase;
    std::uint16_t density;
    std::uint8_t dispersion;
    std::uint8_t thermal_conductivity;
    std::int16_t default_temperature;
    std::int16_t ignition_temperature;
    std::uint8_t blast_resistance;
    MaterialFlags flags;
};

[[nodiscard]] const MaterialDefinition& material_definition(MaterialId id) noexcept;
[[nodiscard]] bool is_dynamic(MaterialId id) noexcept;
[[nodiscard]] bool is_valid(MaterialId id) noexcept;
[[nodiscard]] bool has_flag(MaterialId id, MaterialFlags flag) noexcept;

inline constexpr std::size_t material_count =
    static_cast<std::size_t>(MaterialId::Count);

} // namespace meat2d
