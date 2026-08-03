#pragma once

#include "meat2d/ai/NeuralNetwork.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace meat2d::ai {

inline constexpr std::size_t maximum_learning_agent_actions = 64U;

// A bounded inference policy plus gameplay-facing agent state. Training and
// replay dataset generation remain external; runtime decisions are fixed-
// point, budgeted, and selected deterministically on ties.
class MachineLearningAgent {
  public:
    explicit MachineLearningAgent(std::uint32_t id = 0U,
                                  std::size_t maximum_decisions_per_tick = 1U);

    bool set_policy(FixedNeuralNetwork policy, std::size_t action_count);
    void begin_tick() noexcept;
    [[nodiscard]] std::optional<std::size_t> decide(
        std::span<const std::int32_t> observation) noexcept;
    void record_reward(std::int32_t reward) noexcept;

    [[nodiscard]] std::uint32_t id() const noexcept;
    [[nodiscard]] std::int64_t total_reward() const noexcept;
    [[nodiscard]] std::size_t decisions_this_tick() const noexcept;
    [[nodiscard]] std::uint64_t state_hash() const noexcept;

  private:
    std::uint32_t id_{};
    std::size_t maximum_decisions_per_tick_{};
    std::size_t action_count_{};
    std::size_t decisions_this_tick_{};
    std::int64_t total_reward_{};
    FixedNeuralNetwork policy_;
};

} // namespace meat2d::ai
