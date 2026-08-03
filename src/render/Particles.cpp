#include "meat2d/render/Particles.hpp"

#include <algorithm>
#include <type_traits>

namespace meat2d::render {
namespace {

constexpr std::uint64_t fnv_offset = 14695981039346656037ULL;
constexpr std::uint64_t fnv_prime = 1099511628211ULL;

void hash_byte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= value;
    hash *= fnv_prime;
}

template <typename Integer> void hash_integer(std::uint64_t& hash, Integer value) noexcept {
    using Unsigned = std::make_unsigned_t<Integer>;
    const auto converted = static_cast<Unsigned>(value);
    for (std::size_t offset = 0; offset < sizeof(Integer); ++offset) {
        hash_byte(hash, static_cast<std::uint8_t>(converted >> (offset * 8U)));
    }
}

} // namespace

ParticleSystem::ParticleSystem(std::uint32_t maximum_particles)
    : maximum_particles_(maximum_particles) {
    particles_.reserve(maximum_particles_);
}

ParticleId ParticleSystem::spawn(ParticleConfig config) {
    if (particles_.size() >= maximum_particles_ || next_id_ == invalid_particle) {
        return invalid_particle;
    }
    const auto id = next_id_++;
    particles_.push_back(Particle{
        .id = id,
        .config = config,
        .position = config.position,
        .velocity = config.velocity,
        .age = 0,
        .alive = true,
    });
    return id;
}

void ParticleSystem::step(std::uint32_t ticks) noexcept {
    for (auto& particle : particles_) {
        if (!particle.alive) {
            continue;
        }
        for (std::uint32_t tick = 0; tick < ticks; ++tick) {
            particle.velocity.x += particle.config.acceleration.x;
            particle.velocity.y += particle.config.acceleration.y;
            particle.position.x += particle.velocity.x;
            particle.position.y += particle.velocity.y;
            if (particle.age < particle.config.lifetime_ticks) {
                ++particle.age;
            }
            if (particle.age >= particle.config.lifetime_ticks) {
                particle.alive = false;
                break;
            }
        }
    }
    particles_.erase(
        std::remove_if(particles_.begin(), particles_.end(),
                       [](const Particle& particle) { return !particle.alive; }),
        particles_.end());
}

void ParticleSystem::clear() noexcept {
    particles_.clear();
    next_id_ = 1;
}

std::span<const Particle> ParticleSystem::particles() const noexcept {
    return particles_;
}

std::uint32_t ParticleSystem::maximum_particles() const noexcept {
    return maximum_particles_;
}

std::uint64_t ParticleSystem::state_hash() const noexcept {
    std::uint64_t hash = fnv_offset;
    hash_integer(hash, maximum_particles_);
    hash_integer(hash, next_id_);
    hash_integer(hash, static_cast<std::uint32_t>(particles_.size()));
    for (const auto& particle : particles_) {
        hash_integer(hash, particle.id);
        hash_integer(hash, particle.position.x);
        hash_integer(hash, particle.position.y);
        hash_integer(hash, particle.velocity.x);
        hash_integer(hash, particle.velocity.y);
        hash_integer(hash, particle.config.position.x);
        hash_integer(hash, particle.config.position.y);
        hash_integer(hash, particle.config.velocity.x);
        hash_integer(hash, particle.config.velocity.y);
        hash_integer(hash, particle.config.acceleration.x);
        hash_integer(hash, particle.config.acceleration.y);
        hash_integer(hash, particle.age);
        hash_integer(hash, particle.config.lifetime_ticks);
        hash_integer(hash, particle.config.size);
        hash_byte(hash, particle.config.color.r);
        hash_byte(hash, particle.config.color.g);
        hash_byte(hash, particle.config.color.b);
        hash_byte(hash, particle.config.color.a);
    }
    return hash;
}

} // namespace meat2d::render
