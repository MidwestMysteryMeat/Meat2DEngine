#include "meat2d/ai/LearningAgent.hpp"

#include <algorithm>
#include <limits>
#include <type_traits>
#include <utility>

namespace meat2d::ai {
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
    for (std::size_t index = 0; index < sizeof(Integer); ++index) {
        hash_byte(hash, static_cast<std::uint8_t>(converted >> (index * 8U)));
    }
}

} // namespace

MachineLearningAgent::MachineLearningAgent(std::uint32_t id,
                                           std::size_t maximum_decisions_per_tick)
    : id_(id),
      maximum_decisions_per_tick_(
          std::clamp(maximum_decisions_per_tick, std::size_t{1}, std::size_t{256})) {}

bool MachineLearningAgent::set_policy(FixedNeuralNetwork policy, std::size_t action_count) {
    if (action_count == 0U || action_count > maximum_learning_agent_actions ||
        policy.layers().empty() || policy.layers().back().output_units < action_count) {
        return false;
    }
    policy_ = std::move(policy);
    action_count_ = action_count;
    return true;
}

void MachineLearningAgent::begin_tick() noexcept {
    decisions_this_tick_ = 0;
}

std::optional<std::size_t> MachineLearningAgent::decide(
    std::span<const std::int32_t> observation) noexcept {
    if (action_count_ == 0U || decisions_this_tick_ >= maximum_decisions_per_tick_) {
        return std::nullopt;
    }
    const auto output = policy_.infer(observation);
    if (!output || output->size() < action_count_) {
        return std::nullopt;
    }
    std::size_t best = 0U;
    for (std::size_t action = 1U; action < action_count_; ++action) {
        if ((*output)[action] > (*output)[best]) {
            best = action;
        }
    }
    ++decisions_this_tick_;
    return best;
}

void MachineLearningAgent::record_reward(std::int32_t reward) noexcept {
    total_reward_ = std::clamp<std::int64_t>(
        total_reward_ + reward, std::numeric_limits<std::int64_t>::min(),
        std::numeric_limits<std::int64_t>::max());
}

std::uint32_t MachineLearningAgent::id() const noexcept { return id_; }

std::int64_t MachineLearningAgent::total_reward() const noexcept { return total_reward_; }

std::size_t MachineLearningAgent::decisions_this_tick() const noexcept {
    return decisions_this_tick_;
}

std::uint64_t MachineLearningAgent::state_hash() const noexcept {
    auto hash = policy_.state_hash();
    hash_integer(hash, id_);
    hash_integer(hash, static_cast<std::uint64_t>(action_count_));
    hash_integer(hash, static_cast<std::uint64_t>(decisions_this_tick_));
    hash_integer(hash, total_reward_);
    return hash == 0U ? fnv_offset : hash;
}

} // namespace meat2d::ai
