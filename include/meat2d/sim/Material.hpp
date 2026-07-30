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
    Count
};

enum class MaterialPhase : std::uint8_t {
    Empty,
    Granular,
    Liquid,
    StaticSolid
};

struct MaterialDefinition {
    std::string_view name;
    Rgba8 color;
    MaterialPhase phase;
    std::uint16_t density;
    std::uint8_t dispersion;
};

[[nodiscard]] const MaterialDefinition& material_definition(MaterialId id) noexcept;
[[nodiscard]] bool is_dynamic(MaterialId id) noexcept;
[[nodiscard]] bool is_valid(MaterialId id) noexcept;

inline constexpr std::size_t material_count =
    static_cast<std::size_t>(MaterialId::Count);

} // namespace meat2d
