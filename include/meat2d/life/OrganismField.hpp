#pragma once

#include "meat2d/core/Types.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace meat2d {
class World;
}

namespace meat2d::life {

struct OrganismTraits {
    std::uint8_t photosynthesis{};
    std::uint8_t digestion{};
    std::uint8_t motility{};
    std::uint8_t reproduction{};
    std::uint8_t heat_preference{};
    std::uint8_t resilience{};
    std::uint8_t mutation{};
    std::uint8_t pigment{};
};

[[nodiscard]] constexpr std::uint32_t encode_traits(OrganismTraits traits) noexcept {
    return (static_cast<std::uint32_t>(traits.photosynthesis & 0x0FU)) |
           (static_cast<std::uint32_t>(traits.digestion & 0x0FU) << 4U) |
           (static_cast<std::uint32_t>(traits.motility & 0x0FU) << 8U) |
           (static_cast<std::uint32_t>(traits.reproduction & 0x0FU) << 12U) |
           (static_cast<std::uint32_t>(traits.heat_preference & 0x0FU) << 16U) |
           (static_cast<std::uint32_t>(traits.resilience & 0x0FU) << 20U) |
           (static_cast<std::uint32_t>(traits.mutation & 0x0FU) << 24U) |
           (static_cast<std::uint32_t>(traits.pigment & 0x0FU) << 28U);
}

[[nodiscard]] constexpr OrganismTraits decode_traits(std::uint32_t genome) noexcept {
    return {
        static_cast<std::uint8_t>(genome & 0x0FU),
        static_cast<std::uint8_t>((genome >> 4U) & 0x0FU),
        static_cast<std::uint8_t>((genome >> 8U) & 0x0FU),
        static_cast<std::uint8_t>((genome >> 12U) & 0x0FU),
        static_cast<std::uint8_t>((genome >> 16U) & 0x0FU),
        static_cast<std::uint8_t>((genome >> 20U) & 0x0FU),
        static_cast<std::uint8_t>((genome >> 24U) & 0x0FU),
        static_cast<std::uint8_t>((genome >> 28U) & 0x0FU),
    };
}

inline constexpr std::uint32_t photosynthetic_genome = encode_traits({
    .photosynthesis = 13,
    .digestion = 3,
    .motility = 5,
    .reproduction = 10,
    .heat_preference = 4,
    .resilience = 7,
    .mutation = 5,
    .pigment = 4,
});

inline constexpr std::uint32_t decomposer_genome = encode_traits({
    .photosynthesis = 2,
    .digestion = 14,
    .motility = 9,
    .reproduction = 8,
    .heat_preference = 4,
    .resilience = 8,
    .mutation = 7,
    .pigment = 10,
});

inline constexpr std::uint32_t extremophile_genome = encode_traits({
    .photosynthesis = 6,
    .digestion = 7,
    .motility = 6,
    .reproduction = 7,
    .heat_preference = 12,
    .resilience = 14,
    .mutation = 8,
    .pigment = 13,
});

struct OrganismCell {
    std::uint32_t genome{};
    std::uint16_t energy{};
    std::uint16_t age{};

    friend constexpr bool operator==(const OrganismCell&, const OrganismCell&) = default;
};

static_assert(sizeof(OrganismCell) == 8);

struct OrganismStats {
    std::uint32_t population{};
    std::uint32_t evaluated{};
    std::uint32_t births{};
    std::uint32_t deaths{};
    std::uint32_t moves{};
    std::uint32_t consumed_cells{};
    std::uint32_t mutations{};
};

class OrganismField {
  public:
    OrganismField(std::int32_t width, std::int32_t height, std::uint64_t seed);

    [[nodiscard]] std::int32_t width() const noexcept;
    [[nodiscard]] std::int32_t height() const noexcept;
    [[nodiscard]] std::uint32_t population() const noexcept;
    [[nodiscard]] bool in_bounds(Vec2i position) const noexcept;
    [[nodiscard]] const OrganismCell& cell(Vec2i position) const;
    [[nodiscard]] std::span<const OrganismCell> cells() const noexcept;

    bool seed(Vec2i position, std::uint32_t genome, std::uint16_t energy = 900);
    bool erase(Vec2i position);
    OrganismStats step(World& world);

    [[nodiscard]] Rgba8 color(const OrganismCell& organism) const noexcept;
    [[nodiscard]] std::uint64_t state_hash() const noexcept;

  private:
    [[nodiscard]] std::size_t index(Vec2i position) const noexcept;
    [[nodiscard]] Vec2i choose_destination(
        const World& world,
        Vec2i position,
        const OrganismCell& organism,
        OrganismTraits traits) const;
    [[nodiscard]] bool exposed_to_air(const World& world, Vec2i position) const noexcept;
    [[nodiscard]] bool has_water(const World& world, Vec2i position) const noexcept;
    [[nodiscard]] std::uint64_t noise(
        Vec2i position,
        Tick tick,
        std::uint64_t salt) const noexcept;

    std::int32_t width_{};
    std::int32_t height_{};
    std::uint64_t seed_{};
    std::uint32_t population_{};
    std::vector<OrganismCell> cells_;
    std::vector<OrganismCell> next_;
};

} // namespace meat2d::life
