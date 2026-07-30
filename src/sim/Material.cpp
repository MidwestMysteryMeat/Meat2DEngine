#include "meat2d/sim/Material.hpp"

#include <array>
#include <limits>
#include <type_traits>

namespace meat2d {
namespace {

constexpr auto no_ignition = std::numeric_limits<std::int16_t>::max();
constexpr auto celsius(int value) {
    return static_cast<std::int16_t>(value * 16);
}

constexpr std::array<MaterialDefinition, material_count> definitions{{
    {"empty", {12, 16, 24, 255}, MaterialPhase::Empty, 0, 0, 0, celsius(20),
     no_ignition, 0, MaterialFlags::None},
    {"sand", {218, 183, 95, 255}, MaterialPhase::Granular, 1800, 0, 18, celsius(20),
     no_ignition, 35, MaterialFlags::Destructible},
    {"water", {55, 126, 217, 225}, MaterialPhase::Liquid, 1000, 5, 115, celsius(20),
     no_ignition, 0, MaterialFlags::None},
    {"stone", {91, 96, 110, 255}, MaterialPhase::StaticSolid, 3000, 0, 70, celsius(20),
     no_ignition, 190, MaterialFlags::Corrodible | MaterialFlags::Destructible},
    {"wood", {126, 80, 43, 255}, MaterialPhase::StaticSolid, 700, 0, 22, celsius(20),
     celsius(300), 70,
     MaterialFlags::Flammable | MaterialFlags::Corrodible | MaterialFlags::Organic |
         MaterialFlags::Destructible},
    {"oil", {62, 52, 43, 245}, MaterialPhase::Liquid, 800, 4, 20, celsius(20),
     celsius(180), 0,
     MaterialFlags::Flammable | MaterialFlags::Corrodible | MaterialFlags::Destructible},
    {"fire", {255, 108, 32, 255}, MaterialPhase::Gas, 1, 2, 6, celsius(650),
     no_ignition, 0, MaterialFlags::None},
    {"smoke", {76, 79, 90, 205}, MaterialPhase::Gas, 2, 4, 8, celsius(95), no_ignition,
     0, MaterialFlags::None},
    {"steam", {190, 216, 228, 210}, MaterialPhase::Gas, 1, 5, 28, celsius(110),
     no_ignition, 0, MaterialFlags::None},
    {"metal", {151, 162, 178, 255}, MaterialPhase::StaticSolid, 7800, 0, 235,
     celsius(20), no_ignition, 235,
     MaterialFlags::Conductive | MaterialFlags::Corrodible | MaterialFlags::Destructible},
    {"acid", {108, 232, 77, 235}, MaterialPhase::Liquid, 1120, 3, 90, celsius(20),
     no_ignition, 0, MaterialFlags::None},
    {"plant", {51, 169, 68, 255}, MaterialPhase::StaticSolid, 600, 0, 30, celsius(20),
     celsius(220), 25,
     MaterialFlags::Flammable | MaterialFlags::Corrodible | MaterialFlags::Organic |
         MaterialFlags::Destructible},
    {"seed", {143, 103, 48, 255}, MaterialPhase::Granular, 850, 0, 20, celsius(20),
     celsius(200), 5,
     MaterialFlags::Flammable | MaterialFlags::Corrodible | MaterialFlags::Organic |
         MaterialFlags::Destructible},
    {"soil", {111, 73, 43, 255}, MaterialPhase::Granular, 1450, 0, 35, celsius(20),
     no_ignition, 30, MaterialFlags::Corrodible | MaterialFlags::Destructible},
    {"mud", {89, 64, 47, 255}, MaterialPhase::Liquid, 1550, 1, 55, celsius(20),
     no_ignition, 20, MaterialFlags::Corrodible | MaterialFlags::Destructible},
    {"salt", {237, 235, 224, 255}, MaterialPhase::Granular, 2160, 0, 80, celsius(20),
     no_ignition, 20, MaterialFlags::Corrodible | MaterialFlags::Destructible},
    {"snow", {231, 243, 255, 255}, MaterialPhase::Granular, 350, 0, 18, celsius(-5),
     no_ignition, 5, MaterialFlags::Destructible},
    {"ice", {139, 205, 237, 245}, MaterialPhase::StaticSolid, 920, 0, 95, celsius(-10),
     no_ignition, 45, MaterialFlags::Destructible},
    {"lava", {255, 66, 14, 255}, MaterialPhase::Liquid, 2800, 1, 70, celsius(1100),
     no_ignition, 0, MaterialFlags::None},
    {"obsidian", {42, 27, 57, 255}, MaterialPhase::StaticSolid, 2500, 0, 55, celsius(40),
     no_ignition, 225, MaterialFlags::Destructible},
    {"gunpowder", {57, 52, 46, 255}, MaterialPhase::Granular, 900, 0, 18, celsius(20),
     celsius(170), 0, MaterialFlags::Flammable | MaterialFlags::Destructible},
    {"explosive_gas", {164, 211, 91, 190}, MaterialPhase::Gas, 3, 6, 8, celsius(20),
     celsius(120), 0, MaterialFlags::Flammable},
    {"concrete", {132, 134, 137, 255}, MaterialPhase::StaticSolid, 2400, 0, 65,
     celsius(20), no_ignition, 205,
     MaterialFlags::Corrodible | MaterialFlags::Destructible},
    {"electricity", {105, 194, 255, 255}, MaterialPhase::Energy, 0, 0, 0, celsius(800),
     no_ignition, 0, MaterialFlags::None},
    {"debris", {117, 111, 104, 255}, MaterialPhase::Granular, 1700, 0, 28, celsius(20),
     no_ignition, 15, MaterialFlags::Corrodible | MaterialFlags::Destructible},
}};

static_assert(definitions.size() == material_count);

} // namespace

const MaterialDefinition& material_definition(MaterialId id) noexcept {
    const auto index = static_cast<std::size_t>(id);
    return definitions[index < definitions.size() ? index : 0];
}

bool is_dynamic(MaterialId id) noexcept {
    const auto phase = material_definition(id).phase;
    return phase == MaterialPhase::Granular || phase == MaterialPhase::Liquid ||
           phase == MaterialPhase::Gas;
}

bool is_valid(MaterialId id) noexcept {
    return static_cast<std::size_t>(id) < material_count;
}

bool blocks_line_of_sight(MaterialId id) noexcept {
    const auto phase = material_definition(id).phase;
    return phase == MaterialPhase::Granular || phase == MaterialPhase::Liquid ||
           phase == MaterialPhase::StaticSolid;
}

bool has_flag(MaterialId id, MaterialFlags flag) noexcept {
    using Underlying = std::underlying_type_t<MaterialFlags>;
    const auto value = static_cast<Underlying>(material_definition(id).flags);
    const auto requested = static_cast<Underlying>(flag);
    return (value & requested) == requested;
}

} // namespace meat2d
