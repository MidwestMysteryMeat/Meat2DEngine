#pragma once

#include "meat2d/sim/Material.hpp"

#include <cstdint>
#include <type_traits>

namespace meat2d {

inline constexpr std::int16_t room_temperature = 20 * 16;

// Eight bytes keeps a 64x64 chunk at 32 KiB. Temperature uses 1/16 °C fixed
// point. Velocity is deliberately small and deterministic; no floats enter the
// authoritative cellular state.
struct Cell {
    MaterialId material{MaterialId::Empty};
    std::uint8_t variant{};
    std::uint8_t updated_epoch{};
    std::uint8_t state{};
    std::int16_t temperature{room_temperature};
    std::int8_t velocity_x{};
    std::int8_t velocity_y{};

    friend constexpr bool operator==(const Cell&, const Cell&) = default;
};

static_assert(sizeof(Cell) == 8);
static_assert(std::is_trivially_copyable_v<Cell>);

} // namespace meat2d
