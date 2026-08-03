#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>

namespace meat2d::core {

struct FixedStepResult {
    std::uint32_t steps{};
    double interpolation_alpha{};
    bool dropped_time{};
};

// Frame pacing belongs to the host application, while the simulation remains
// driven by integer ticks. The accumulator bounds catch-up work so a stalled
// render thread cannot create an unbounded simulation spiral.
class FixedTimestep {
  public:
    explicit FixedTimestep(std::uint32_t ticks_per_second = 60,
                           std::uint32_t max_steps_per_advance = 8) noexcept;

    void reset() noexcept;

    [[nodiscard]] std::uint32_t ticks_per_second() const noexcept;
    [[nodiscard]] std::uint32_t max_steps_per_advance() const noexcept;
    [[nodiscard]] std::chrono::nanoseconds fixed_interval() const noexcept;

    [[nodiscard]] FixedStepResult advance(std::chrono::nanoseconds elapsed) noexcept;

  private:
    static constexpr std::uint32_t maximum_ticks_per_second = 1'000U;
    static constexpr std::uint32_t maximum_steps_per_advance = 1'024U;

    std::uint32_t ticks_per_second_{};
    std::uint32_t max_steps_per_advance_{};
    std::uint64_t interval_nanoseconds_{};
    std::uint64_t accumulator_nanoseconds_{};
};

inline FixedTimestep::FixedTimestep(std::uint32_t ticks_per_second,
                                    std::uint32_t max_steps_per_advance) noexcept
    : ticks_per_second_(std::clamp(ticks_per_second, 1U, maximum_ticks_per_second)),
      max_steps_per_advance_(
          std::clamp(max_steps_per_advance, 1U, maximum_steps_per_advance)),
      interval_nanoseconds_(1'000'000'000ULL / ticks_per_second_) {}

inline void FixedTimestep::reset() noexcept {
    accumulator_nanoseconds_ = 0;
}

inline std::uint32_t FixedTimestep::ticks_per_second() const noexcept {
    return ticks_per_second_;
}

inline std::uint32_t FixedTimestep::max_steps_per_advance() const noexcept {
    return max_steps_per_advance_;
}

inline std::chrono::nanoseconds FixedTimestep::fixed_interval() const noexcept {
    return std::chrono::nanoseconds(interval_nanoseconds_);
}

inline FixedStepResult FixedTimestep::advance(std::chrono::nanoseconds elapsed) noexcept {
    const auto elapsed_count = elapsed.count();
    bool dropped_time = false;
    if (elapsed_count > 0) {
        const auto maximum_accumulator =
            interval_nanoseconds_ * static_cast<std::uint64_t>(max_steps_per_advance_);
        const auto available = maximum_accumulator - accumulator_nanoseconds_;
        const auto requested = static_cast<std::uint64_t>(elapsed_count);
        if (requested > available) {
            accumulator_nanoseconds_ = maximum_accumulator;
            dropped_time = true;
        } else {
            accumulator_nanoseconds_ += requested;
        }
    }

    const auto available_steps = accumulator_nanoseconds_ / interval_nanoseconds_;
    const auto steps = static_cast<std::uint32_t>(std::min<std::uint64_t>(
        available_steps, static_cast<std::uint64_t>(max_steps_per_advance_)));
    accumulator_nanoseconds_ -=
        interval_nanoseconds_ * static_cast<std::uint64_t>(steps);
    return FixedStepResult{
        .steps = steps,
        .interpolation_alpha = static_cast<double>(accumulator_nanoseconds_) /
                               static_cast<double>(interval_nanoseconds_),
        .dropped_time = dropped_time,
    };
}

} // namespace meat2d::core
