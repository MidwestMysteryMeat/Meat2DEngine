#pragma once

#include <cstdint>
#include <limits>

namespace meat2d {

// Small deterministic RNG for authoritative gameplay and sandboxed scripts.
// It has no platform or time dependency, and uniform(bound) avoids modulo
// bias with rejection sampling.
class DeterministicRng {
  public:
    explicit constexpr DeterministicRng(std::uint64_t seed = 1U) noexcept
        : state_(seed == 0U ? 1U : seed) {}

    [[nodiscard]] constexpr std::uint64_t state() const noexcept { return state_; }

    constexpr void reseed(std::uint64_t seed) noexcept { state_ = seed == 0U ? 1U : seed; }

    [[nodiscard]] constexpr std::uint64_t next_u64() noexcept {
        state_ ^= state_ >> 12U;
        state_ ^= state_ << 25U;
        state_ ^= state_ >> 27U;
        return state_ * 2'685'821'657'736'338'717ULL;
    }

    [[nodiscard]] constexpr std::uint32_t next_u32() noexcept {
        return static_cast<std::uint32_t>(next_u64() >> 32U);
    }

    [[nodiscard]] constexpr std::uint32_t uniform(std::uint32_t bound) noexcept {
        if (bound == 0U) {
            return 0U;
        }
        const auto limit = static_cast<std::uint32_t>(
            std::numeric_limits<std::uint32_t>::max() -
            (std::numeric_limits<std::uint32_t>::max() % bound));
        std::uint32_t value{};
        do {
            value = next_u32();
        } while (value >= limit);
        return value % bound;
    }

    [[nodiscard]] constexpr bool chance(std::uint32_t numerator,
                                        std::uint32_t denominator) noexcept {
        if (denominator == 0U) {
            return false;
        }
        return numerator >= denominator || uniform(denominator) < numerator;
    }

  private:
    std::uint64_t state_{};
};

} // namespace meat2d
