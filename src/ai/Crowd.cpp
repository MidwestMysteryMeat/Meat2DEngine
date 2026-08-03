#include "meat2d/ai/Crowd.hpp"

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

std::int32_t direction(std::int32_t value) noexcept {
    return value < 0 ? -1 : value > 0 ? 1 : 0;
}

} // namespace

CrowdSimulation::CrowdSimulation(std::size_t max_agents)
    : maximum_agents_(std::clamp(max_agents, std::size_t{1}, maximum_crowd_agents)) {
    agents_.reserve(maximum_agents_);
}

bool CrowdSimulation::add_agent(CrowdAgent agent) {
    if (agent.id == 0U || find(agent.id) != nullptr || agents_.size() == maximum_agents_ ||
        agent.max_step < 0 || agent.separation_radius < 0) {
        return false;
    }
    agents_.push_back(agent);
    std::sort(agents_.begin(), agents_.end(), [](const auto& left, const auto& right) {
        return left.id < right.id;
    });
    return true;
}

bool CrowdSimulation::remove_agent(std::uint32_t id) {
    const auto iterator = std::find_if(agents_.begin(), agents_.end(), [id](const auto& agent) {
        return agent.id == id;
    });
    if (iterator == agents_.end()) {
        return false;
    }
    agents_.erase(iterator);
    return true;
}

bool CrowdSimulation::set_target(std::uint32_t id, Vec2i target) {
    auto* agent = find(id);
    if (agent == nullptr) {
        return false;
    }
    agent->target = target;
    return true;
}

CrowdAgent* CrowdSimulation::find(std::uint32_t id) noexcept {
    const auto iterator = std::find_if(agents_.begin(), agents_.end(), [id](const auto& agent) {
        return agent.id == id;
    });
    return iterator == agents_.end() ? nullptr : &*iterator;
}

const CrowdAgent* CrowdSimulation::find(std::uint32_t id) const noexcept {
    const auto iterator = std::find_if(agents_.begin(), agents_.end(), [id](const auto& agent) {
        return agent.id == id;
    });
    return iterator == agents_.end() ? nullptr : &*iterator;
}

std::span<const CrowdAgent> CrowdSimulation::agents() const noexcept {
    return std::span<const CrowdAgent>(agents_);
}

CrowdStepStats CrowdSimulation::step(RectI bounds) {
    CrowdStepStats stats{.agents = static_cast<std::uint32_t>(agents_.size())};
    if (bounds.empty()) {
        return stats;
    }
    for (auto& agent : agents_) {
        if (!agent.enabled || agent.max_step == 0) {
            continue;
        }
        Vec2i delta{direction(agent.target.x - agent.position.x),
                    direction(agent.target.y - agent.position.y)};
        bool separated = false;
        for (const auto& other : agents_) {
            if (other.id == agent.id || !other.enabled) {
                continue;
            }
            const auto dx = agent.position.x - other.position.x;
            const auto dy = agent.position.y - other.position.y;
            if (std::abs(dx) <= agent.separation_radius &&
                std::abs(dy) <= agent.separation_radius) {
                delta.x += direction(dx);
                delta.y += direction(dy);
                separated = true;
            }
        }
        delta.x = std::clamp(delta.x, -agent.max_step, agent.max_step);
        delta.y = std::clamp(delta.y, -agent.max_step, agent.max_step);
        const auto next = Vec2i{
            std::clamp(agent.position.x + delta.x, bounds.x, bounds.x + bounds.width - 1),
            std::clamp(agent.position.y + delta.y, bounds.y, bounds.y + bounds.height - 1),
        };
        if (next != agent.position) {
            agent.position = next;
            ++stats.moved;
        }
        if (separated) {
            ++stats.separated;
        }
    }
    return stats;
}

std::uint64_t CrowdSimulation::state_hash() const noexcept {
    std::uint64_t hash = fnv_offset;
    hash_integer(hash, static_cast<std::uint32_t>(agents_.size()));
    for (const auto& agent : agents_) {
        hash_integer(hash, agent.id);
        hash_integer(hash, agent.position.x);
        hash_integer(hash, agent.position.y);
        hash_integer(hash, agent.target.x);
        hash_integer(hash, agent.target.y);
        hash_integer(hash, agent.max_step);
        hash_integer(hash, agent.separation_radius);
        hash_byte(hash, static_cast<std::uint8_t>(agent.enabled));
    }
    return hash;
}

} // namespace meat2d::ai
