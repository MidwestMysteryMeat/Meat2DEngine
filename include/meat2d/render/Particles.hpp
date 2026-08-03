#pragma once

#include "meat2d/core/Types.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace meat2d::render {

using ParticleId = std::uint32_t;
inline constexpr ParticleId invalid_particle = 0;

struct ParticleConfig {
    Vec2i position{};
    Vec2i velocity{};
    Vec2i acceleration{};
    std::uint32_t lifetime_ticks{60};
    std::uint16_t size{1};
    Rgba8 color{};
};

struct Particle {
    ParticleId id{};
    ParticleConfig config{};
    Vec2i position{};
    Vec2i velocity{};
    std::uint32_t age{};
    bool alive{true};
};

class ParticleSystem {
  public:
    explicit ParticleSystem(std::uint32_t maximum_particles = 4096);

    [[nodiscard]] ParticleId spawn(ParticleConfig config);
    void step(std::uint32_t ticks = 1) noexcept;
    void clear() noexcept;

    [[nodiscard]] std::span<const Particle> particles() const noexcept;
    [[nodiscard]] std::uint32_t maximum_particles() const noexcept;
    [[nodiscard]] std::uint64_t state_hash() const noexcept;

  private:
    std::uint32_t maximum_particles_{};
    ParticleId next_id_{1};
    std::vector<Particle> particles_;
};

} // namespace meat2d::render
