#pragma once

#include "meat2d/ai/LearningAgent.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace meat2d::ai {

inline constexpr std::size_t maximum_learning_observations = 256U;
inline constexpr std::size_t maximum_learning_episode_steps = 65'536U;

struct LearningEnvironmentConfig {
    std::size_t observation_units{};
    std::size_t maximum_episode_steps{1'024U};
};

struct LearningTransition {
    std::uint32_t step{};
    std::vector<std::int32_t> observation;
    std::size_t action{};
    std::int32_t reward{};
    bool terminal{};
};

// A deterministic, engine-owned environment seam. Game templates supply
// observations and apply the returned action through their normal command
// validators; this class only enforces bounds and records transitions.
class LearningEnvironment {
  public:
    explicit LearningEnvironment(LearningEnvironmentConfig config);

    [[nodiscard]] bool valid() const noexcept;
    bool begin_episode() noexcept;
    [[nodiscard]] std::optional<std::size_t> choose_action(
        MachineLearningAgent& agent, std::span<const std::int32_t> observation);
    bool finish_step(std::int32_t reward, bool terminal);

    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] std::uint64_t episode_index() const noexcept;
    [[nodiscard]] std::size_t steps() const noexcept;
    [[nodiscard]] std::span<const LearningTransition> transitions() const noexcept;
    [[nodiscard]] std::uint64_t state_hash() const noexcept;

  private:
    LearningEnvironmentConfig config_{};
    bool valid_{};
    bool active_{};
    std::uint64_t episode_index_{};
    std::size_t steps_{};
    std::optional<LearningTransition> pending_;
    std::vector<LearningTransition> transitions_;
};

} // namespace meat2d::ai
