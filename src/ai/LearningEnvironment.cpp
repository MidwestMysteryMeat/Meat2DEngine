#include "meat2d/ai/LearningEnvironment.hpp"

#include <algorithm>
#include <limits>

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

LearningEnvironment::LearningEnvironment(LearningEnvironmentConfig config) : config_(config) {
    valid_ = config_.observation_units > 0U &&
             config_.observation_units <= maximum_learning_observations &&
             config_.maximum_episode_steps > 0U &&
             config_.maximum_episode_steps <= maximum_learning_episode_steps &&
             config_.maximum_episode_steps <= std::numeric_limits<std::uint32_t>::max();
    if (valid_) {
        transitions_.reserve(config_.maximum_episode_steps);
    }
}

bool LearningEnvironment::valid() const noexcept {
    return valid_;
}

bool LearningEnvironment::begin_episode() noexcept {
    if (!valid_ || episode_index_ == std::numeric_limits<std::uint64_t>::max()) {
        return false;
    }
    ++episode_index_;
    active_ = true;
    steps_ = 0U;
    pending_.reset();
    transitions_.clear();
    return true;
}

std::optional<std::size_t> LearningEnvironment::choose_action(
    MachineLearningAgent& agent, std::span<const std::int32_t> observation) {
    if (!active_ || pending_.has_value() || observation.size() != config_.observation_units ||
        steps_ >= config_.maximum_episode_steps) {
        return std::nullopt;
    }
    agent.begin_tick();
    const auto action = agent.decide(observation);
    if (!action) {
        return std::nullopt;
    }
    pending_ = LearningTransition{.step = static_cast<std::uint32_t>(steps_),
                                  .observation = std::vector<std::int32_t>(observation.begin(),
                                                                           observation.end()),
                                  .action = *action};
    return action;
}

bool LearningEnvironment::finish_step(std::int32_t reward, bool terminal) {
    if (!active_ || !pending_) {
        return false;
    }
    pending_->reward = reward;
    pending_->terminal = terminal;
    transitions_.push_back(std::move(*pending_));
    pending_.reset();
    ++steps_;
    if (terminal || steps_ >= config_.maximum_episode_steps) {
        active_ = false;
    }
    return true;
}

bool LearningEnvironment::active() const noexcept {
    return active_;
}

std::uint64_t LearningEnvironment::episode_index() const noexcept {
    return episode_index_;
}

std::size_t LearningEnvironment::steps() const noexcept {
    return steps_;
}

std::span<const LearningTransition> LearningEnvironment::transitions() const noexcept {
    return std::span<const LearningTransition>(transitions_);
}

std::uint64_t LearningEnvironment::state_hash() const noexcept {
    std::uint64_t hash = fnv_offset;
    hash_integer(hash, static_cast<std::uint8_t>(valid_));
    hash_integer(hash, static_cast<std::uint8_t>(active_));
    hash_integer(hash, config_.observation_units);
    hash_integer(hash, config_.maximum_episode_steps);
    hash_integer(hash, episode_index_);
    hash_integer(hash, steps_);
    hash_integer(hash, static_cast<std::uint8_t>(pending_.has_value()));
    if (pending_) {
        hash_integer(hash, pending_->step);
        hash_integer(hash, pending_->action);
        for (const auto value : pending_->observation) {
            hash_integer(hash, value);
        }
    }
    hash_integer(hash, transitions_.size());
    for (const auto& transition : transitions_) {
        hash_integer(hash, transition.step);
        hash_integer(hash, transition.action);
        hash_integer(hash, transition.reward);
        hash_integer(hash, static_cast<std::uint8_t>(transition.terminal));
        for (const auto value : transition.observation) {
            hash_integer(hash, value);
        }
    }
    return hash;
}

} // namespace meat2d::ai
