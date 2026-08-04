#include "meat2d/ai/LivingSimulation.hpp"

#include <algorithm>
#include <limits>
#include <tuple>

namespace meat2d::ai {
namespace {

constexpr std::uint64_t fnv_offset = 14695981039346656037ULL;
constexpr std::uint64_t fnv_prime = 1099511628211ULL;
std::int32_t absolute(std::int32_t value) noexcept {
    return value < 0 ? -value : value;
}

void hash_byte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= value;
    hash *= fnv_prime;
}

template <typename Integer>
void hash_integer(std::uint64_t& hash, Integer value) noexcept {
    using Unsigned = std::make_unsigned_t<Integer>;
    const auto converted = static_cast<Unsigned>(value);
    for (std::size_t index = 0; index < sizeof(Integer); ++index) {
        hash_byte(hash, static_cast<std::uint8_t>(converted >> (index * 8U)));
    }
}

bool command_order(const EntityCommand& left, const EntityCommand& right) noexcept {
    return std::tie(
               left.target_tick,
               left.issuer,
               left.sequence,
               left.type,
               left.target_entity,
               left.target.y,
               left.target.x,
               left.material,
               left.action) <
           std::tie(
               right.target_tick,
               right.issuer,
               right.sequence,
               right.type,
               right.target_entity,
               right.target.y,
               right.target.x,
               right.material,
               right.action);
}

} // namespace

LivingSimulation::LivingSimulation(WorldConfig config)
    : world_(config), organisms_(config.width, config.height, config.seed) {
    agents_.reserve(128);
    commands_.reserve(256);
}

World& LivingSimulation::world() noexcept {
    return world_;
}

const World& LivingSimulation::world() const noexcept {
    return world_;
}

life::OrganismField& LivingSimulation::organisms() noexcept {
    return organisms_;
}

const life::OrganismField& LivingSimulation::organisms() const noexcept {
    return organisms_;
}

std::span<const Agent> LivingSimulation::agents() const noexcept {
    return agents_;
}

const Agent* LivingSimulation::find_agent(EntityId id) const noexcept {
    const auto found = std::find_if(
        agents_.begin(), agents_.end(), [id](const Agent& agent) { return agent.id == id; });
    return found == agents_.end() ? nullptr : &*found;
}

EntityId LivingSimulation::spawn_agent(
    AgentKind kind,
    Vec2i position,
    std::uint32_t genome,
    std::uint16_t generation) {
    if (agents_.size() >= maximum_agents || !can_occupy(position) || occupied(position)) {
        return 0;
    }

    const auto id = next_entity_id_++;
    if (genome == 0U) {
        std::uint64_t value = world_.seed() ^ static_cast<std::uint64_t>(id);
        value ^= value >> 30U;
        value *= 0xBF58476D1CE4E5B9ULL;
        value ^= value >> 27U;
        genome = static_cast<std::uint32_t>(value ^ (value >> 31U));
    }

    Agent agent{};
    agent.id = id;
    agent.kind = kind;
    agent.position = position;
    agent.genome = genome;
    agent.generation = generation;
    switch (kind) {
    case AgentKind::Grazer:
        agent.hunger = 420;
        agent.thirst = 280;
        break;
    case AgentKind::Predator:
        agent.hunger = 520;
        agent.thirst = 240;
        break;
    case AgentKind::Worker:
        agent.hunger = 180;
        agent.thirst = 180;
        break;
    }
    agents_.push_back(agent);
    return id;
}

bool LivingSimulation::queue_command(EntityCommand command) {
    if (command.target_tick <= world_.current_tick() || commands_.size() >= 4'096U ||
        !is_valid(command.material)) {
        return false;
    }
    const auto insertion = std::lower_bound(
        commands_.begin(), commands_.end(), command, command_order);
    commands_.insert(insertion, command);
    return true;
}

LivingStats LivingSimulation::step() {
    LivingStats stats{};
    const auto target_tick = world_.current_tick() + 1U;

    for (auto& agent : agents_) {
        update_needs(agent);
    }

    for (const auto& agent : agents_) {
        if (agent.health == 0U) {
            continue;
        }
        const bool externally_controlled = std::any_of(
            commands_.begin(), commands_.end(), [&](const EntityCommand& command) {
                return command.target_tick == target_tick && command.issuer == agent.id;
            });
        if (externally_controlled) {
            continue;
        }
        if (const auto command = plan_agent(agent)) {
            commands_.push_back(*command);
            ++stats.planned_commands;
        }
    }

    std::stable_sort(commands_.begin(), commands_.end(), command_order);
    std::vector<EntityCommand> future_commands;
    future_commands.reserve(commands_.size());
    for (const auto& command : commands_) {
        if (command.target_tick < target_tick) {
            ++stats.rejected_commands;
        } else if (command.target_tick == target_tick) {
            if (apply_command(command, stats)) {
                ++stats.applied_commands;
            } else {
                ++stats.rejected_commands;
            }
        } else {
            future_commands.push_back(command);
        }
    }
    commands_ = std::move(future_commands);

    stats.world = world_.step();
    stats.organisms = organisms_.step(world_);
    remove_dead(stats);
    return stats;
}

std::uint64_t LivingSimulation::state_hash() const noexcept {
    std::uint64_t hash = fnv_offset;
    hash_integer(hash, world_.state_hash());
    hash_integer(hash, organisms_.state_hash());
    hash_integer(hash, next_entity_id_);
    for (const auto& agent : agents_) {
        hash_integer(hash, agent.id);
        hash_integer(hash, static_cast<std::uint8_t>(agent.kind));
        hash_integer(hash, static_cast<std::uint8_t>(agent.action));
        hash_integer(hash, agent.position.x);
        hash_integer(hash, agent.position.y);
        hash_integer(hash, agent.genome);
        hash_integer(hash, agent.age);
        hash_integer(hash, agent.health);
        hash_integer(hash, agent.hunger);
        hash_integer(hash, agent.thirst);
        hash_integer(hash, agent.generation);
        hash_integer(hash, static_cast<std::uint8_t>(agent.carried));
    }
    for (const auto& command : commands_) {
        hash_integer(hash, command.target_tick);
        hash_integer(hash, command.issuer);
        hash_integer(hash, command.sequence);
        hash_integer(hash, static_cast<std::uint8_t>(command.type));
        hash_integer(hash, command.target_entity);
        hash_integer(hash, command.target.x);
        hash_integer(hash, command.target.y);
        hash_integer(hash, static_cast<std::uint8_t>(command.material));
        hash_integer(hash, static_cast<std::uint8_t>(command.action));
    }
    return hash;
}


bool LivingSimulation::apply_command(const EntityCommand& command, LivingStats& stats) {
    if (command.type == CommandType::Paint) {
        return world_.set_material(command.target, command.material);
    }

    auto* agent = find_agent_mutable(command.issuer);
    if (agent == nullptr || agent->health == 0U) {
        return false;
    }

    switch (command.type) {
    case CommandType::Move: {
        const auto dx = absolute(command.target.x - agent->position.x);
        const auto dy = absolute(command.target.y - agent->position.y);
        if (dx > 1 || dy > 1 || dx + dy == 0 || !can_occupy(command.target) ||
            occupied(command.target, agent->id)) {
            return false;
        }
        agent->position = command.target;
        agent->action =
            command.action == AgentAction::Flee ? AgentAction::Flee : AgentAction::Move;
        return true;
    }
    case CommandType::Consume:
        if (!adjacent(agent->position, command.target) ||
            world_.material(command.target) != command.material) {
            return false;
        }
        if (command.material == MaterialId::Plant && agent->kind == AgentKind::Grazer) {
            world_.set_material(command.target, MaterialId::Empty);
            agent->hunger =
                agent->hunger > 480U ? static_cast<std::uint16_t>(agent->hunger - 480U) : 0U;
            agent->action = AgentAction::Eat;
            return true;
        }
        if (command.material == MaterialId::Water) {
            agent->thirst = 0U;
            agent->action = AgentAction::Drink;
            return true;
        }
        return false;
    case CommandType::Attack: {
        auto* target = find_agent_mutable(command.target_entity);
        if (target == nullptr || target->kind != AgentKind::Grazer ||
            !adjacent(agent->position, target->position)) {
            return false;
        }
        target->health =
            target->health > 25U ? static_cast<std::uint16_t>(target->health - 25U) : 0U;
        agent->hunger =
            agent->hunger > 180U ? static_cast<std::uint16_t>(agent->hunger - 180U) : 0U;
        agent->action = AgentAction::Attack;
        return true;
    }
    case CommandType::Dig: {
        const auto material_id = world_.material(command.target);
        if (agent->kind != AgentKind::Worker || agent->carried != MaterialId::Empty ||
            !adjacent(agent->position, command.target) ||
            !has_flag(material_id, MaterialFlags::Destructible)) {
            return false;
        }
        agent->carried = material_id == MaterialId::Stone ||
                                 material_id == MaterialId::Concrete
                             ? MaterialId::Debris
                             : material_id;
        world_.set_material(command.target, MaterialId::Empty);
        agent->action = AgentAction::Dig;
        return true;
    }
    case CommandType::Place:
        if (agent->kind != AgentKind::Worker || agent->carried == MaterialId::Empty ||
            !adjacent(agent->position, command.target) ||
            world_.material(command.target) != MaterialId::Empty) {
            return false;
        }
        world_.set_material(command.target, agent->carried);
        agent->carried = MaterialId::Empty;
        agent->action = AgentAction::Place;
        return true;
    case CommandType::Reproduce: {
        if (!adjacent(agent->position, command.target) || !can_occupy(command.target) ||
            occupied(command.target, agent->id) || agents_.size() >= maximum_agents) {
            return false;
        }
        auto child_genome = agent->genome;
        const auto mutation_bit =
            static_cast<std::uint32_t>(agent_noise(*agent, 0x4D5554415445ULL) & 31U);
        child_genome ^= 1U << mutation_bit;
        const auto child = spawn_agent(
            agent->kind,
            command.target,
            child_genome,
            static_cast<std::uint16_t>(agent->generation + 1U));
        if (child == 0U) {
            return false;
        }
        agent = find_agent_mutable(command.issuer);
        if (agent != nullptr) {
            agent->hunger = static_cast<std::uint16_t>(
                std::min<std::uint32_t>(1'500U, agent->hunger + 300U));
            agent->action = AgentAction::Reproduce;
        }
        ++stats.births;
        return true;
    }
    case CommandType::Paint:
        return false;
    }
    return false;
}

void LivingSimulation::remove_dead(LivingStats& stats) {
    const auto previous_size = agents_.size();
    agents_.erase(
        std::remove_if(
            agents_.begin(), agents_.end(), [](const Agent& agent) { return agent.health == 0U; }),
        agents_.end());
    stats.deaths = static_cast<std::uint32_t>(previous_size - agents_.size());
}

std::uint64_t LivingSimulation::agent_noise(
    const Agent& agent,
    std::uint64_t salt) const noexcept {
    std::uint64_t value =
        world_.seed() ^ world_.current_tick() ^ salt ^ static_cast<std::uint64_t>(agent.id);
    value ^= static_cast<std::uint64_t>(agent.genome) << 17U;
    value ^= value >> 30U;
    value *= 0xBF58476D1CE4E5B9ULL;
    value ^= value >> 27U;
    value *= 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

} // namespace meat2d::ai
