#include "meat2d/sim/Material.hpp"

#include <array>

namespace meat2d {
namespace {

constexpr std::array<MaterialDefinition, material_count> definitions{{
    {"empty", {12, 16, 24, 255}, MaterialPhase::Empty, 0, 0},
    {"sand", {218, 183, 95, 255}, MaterialPhase::Granular, 1800, 0},
    {"water", {55, 126, 217, 225}, MaterialPhase::Liquid, 1000, 5},
    {"stone", {91, 96, 110, 255}, MaterialPhase::StaticSolid, 3000, 0},
}};

static_assert(definitions.size() == material_count);

} // namespace

const MaterialDefinition& material_definition(MaterialId id) noexcept {
    const auto index = static_cast<std::size_t>(id);
    return definitions[index < definitions.size() ? index : 0];
}

bool is_dynamic(MaterialId id) noexcept {
    const auto phase = material_definition(id).phase;
    return phase == MaterialPhase::Granular || phase == MaterialPhase::Liquid;
}

bool is_valid(MaterialId id) noexcept {
    return static_cast<std::size_t>(id) < material_count;
}

} // namespace meat2d
