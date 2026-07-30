#include "meat2d/ai/LivingSimulation.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <tuple>

namespace meat2d::ai {
namespace {

constexpr std::uint64_t fnv_offset = 14695981039346656037ULL;
constexpr std::uint64_t fnv_prime = 1099511628211ULL;
constexpr std::array<Vec2i, 8> adjacent_directions{{
    {0, 1},
    {-1, 0},
    {1, 0},
    {0, -1},
    {-1, 1},
    {1, 1},
    {-1, -1},
    {1, -1},
}};

std::int32_t absolute(std::int32_t value) noexcept {
    return value < 0 ? -value : value;
}

std::int32_t distance(Vec2i first, Vec2i second) noexcept {
    return absolute(first.x - second.x) + absolute(first.y - second.y);
}

std::int32_t sign(std::int32_t value) noexcept {
    return (value > 0) - (value < 0);
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

Agent* LivingSimulation::find_agent_mutable(EntityId id) noexcept {
    const auto found = std::find_if(
        agents_.begin(), agents_.end(), [id](const Agent& agent) { return agent.id == id; });
    return found == agents_.end() ? nullptr : &*found;
}

bool LivingSimulation::can_occupy(Vec2i position) const noexcept {
    if (!world_.in_bounds(position)) {
        return false;
    }
    const auto phase = material_definition(world_.material(position)).phase;
    return phase == MaterialPhase::Empty || phase == MaterialPhase::Liquid ||
           phase == MaterialPhase::Gas || phase == MaterialPhase::Energy;
}

bool LivingSimulation::occupied(Vec2i position, EntityId ignored) const noexcept {
    return std::any_of(agents_.begin(), agents_.end(), [&](const Agent& agent) {
        return agent.id != ignored && agent.health > 0U && agent.position == position;
    });
}

bool LivingSimulation::adjacent(Vec2i first, Vec2i second) const noexcept {
    const auto dx = absolute(first.x - second.x);
    const auto dy = absolute(first.y - second.y);
    return dx <= 1 && dy <= 1 && (dx != 0 || dy != 0);
}

std::optional<Vec2i> LivingSimulation::nearest_material(
    Vec2i origin,
    MaterialId material_id,
    std::int32_t range) const {
    std::optional<Vec2i> best;
    auto best_distance = std::numeric_limits<std::int32_t>::max();
    for (std::int32_t y = origin.y - range; y <= origin.y + range; ++y) {
        for (std::int32_t x = origin.x - range; x <= origin.x + range; ++x) {
            const Vec2i candidate{x, y};
            const auto candidate_distance = distance(origin, candidate);
            if (candidate_distance > range || !world_.in_bounds(candidate) ||
                world_.material(candidate) != material_id) {
                continue;
            }
            if (candidate_distance < best_distance) {
                best = candidate;
                best_distance = candidate_distance;
            }
        }
    }
    return best;
}

const Agent* LivingSimulation::nearest_agent(
    Vec2i origin,
    AgentKind kind,
    std::int32_t range,
    EntityId ignored) const noexcept {
    const Agent* best = nullptr;
    auto best_distance = std::numeric_limits<std::int32_t>::max();
    for (const auto& candidate : agents_) {
        if (candidate.id == ignored || candidate.kind != kind || candidate.health == 0U) {
            continue;
        }
        const auto candidate_distance = distance(origin, candidate.position);
        if (candidate_distance <= range &&
            (candidate_distance < best_distance ||
             (candidate_distance == best_distance &&
              (best == nullptr || candidate.id < best->id)))) {
            best = &candidate;
            best_distance = candidate_distance;
        }
    }
    return best;
}

std::optional<Vec2i> LivingSimulation::nearest_danger(
    Vec2i origin,
    std::int32_t range) const {
    std::optional<Vec2i> best;
    auto best_distance = std::numeric_limits<std::int32_t>::max();
    for (const auto material_id :
         {MaterialId::Fire, MaterialId::Lava, MaterialId::Acid, MaterialId::Electricity}) {
        const auto candidate = nearest_material(origin, material_id, range);
        if (candidate && distance(origin, *candidate) < best_distance) {
            best = candidate;
            best_distance = distance(origin, *candidate);
        }
    }
    return best;
}

std::optional<EntityCommand> LivingSimulation::plan_agent(const Agent& agent) const {
    const auto target_tick = world_.current_tick() + 1U;
    if (const auto below = Vec2i{agent.position.x, agent.position.y + 1};
        can_occupy(below) && !occupied(below, agent.id)) {
        return EntityCommand{
            .target_tick = target_tick,
            .issuer = agent.id,
            .type = CommandType::Move,
            .target = below,
        };
    }

    if (const auto danger = nearest_danger(agent.position, 7)) {
        const Vec2i away{
            agent.position.x + sign(agent.position.x - danger->x) * 8,
            agent.position.y,
        };
        if (const auto movement = plan_move_toward(agent, away, AgentAction::Flee)) {
            return movement;
        }
    }

    if (agent.age > 900U && agent.hunger < 360U && agent.thirst < 480U &&
        (agent_noise(agent, 0x4252454544ULL) % 600U) == 0U) {
        if (const auto position = reproduction_position(agent)) {
            return EntityCommand{
                .target_tick = target_tick,
                .issuer = agent.id,
                .type = CommandType::Reproduce,
                .target = *position,
            };
        }
    }

    switch (agent.kind) {
    case AgentKind::Grazer:
        if (const auto plant = nearest_material(agent.position, MaterialId::Plant, 1)) {
            return EntityCommand{
                .target_tick = target_tick,
                .issuer = agent.id,
                .type = CommandType::Consume,
                .target = *plant,
                .material = MaterialId::Plant,
            };
        }
        if (agent.thirst >= 500U) {
            if (const auto water = nearest_material(agent.position, MaterialId::Water, 1)) {
                return EntityCommand{
                    .target_tick = target_tick,
                    .issuer = agent.id,
                    .type = CommandType::Consume,
                    .target = *water,
                    .material = MaterialId::Water,
                };
            }
        }
        if (agent.hunger >= agent.thirst) {
            if (const auto plant = nearest_material(agent.position, MaterialId::Plant, 32)) {
                return plan_move_toward(agent, *plant, AgentAction::Move);
            }
        } else if (const auto water =
                       nearest_material(agent.position, MaterialId::Water, 32)) {
            return plan_move_toward(agent, *water, AgentAction::Move);
        }
        break;
    case AgentKind::Predator:
        if (const auto* prey =
                nearest_agent(agent.position, AgentKind::Grazer, 1, agent.id)) {
            return EntityCommand{
                .target_tick = target_tick,
                .issuer = agent.id,
                .type = CommandType::Attack,
                .target_entity = prey->id,
                .target = prey->position,
            };
        }
        if (const auto* prey =
                nearest_agent(agent.position, AgentKind::Grazer, 48, agent.id)) {
            return plan_move_toward(agent, prey->position, AgentAction::Move);
        }
        break;
    case AgentKind::Worker:
        if (agent.carried != MaterialId::Empty) {
            for (const auto direction : adjacent_directions) {
                const Vec2i target{
                    agent.position.x + direction.x,
                    agent.position.y + direction.y,
                };
                const Vec2i support{target.x, target.y + 1};
                if (world_.in_bounds(target) &&
                    world_.material(target) == MaterialId::Empty &&
                    world_.in_bounds(support) && !can_occupy(support) &&
                    !occupied(target, agent.id)) {
                    return EntityCommand{
                        .target_tick = target_tick,
                        .issuer = agent.id,
                        .type = CommandType::Place,
                        .target = target,
                        .material = agent.carried,
                    };
                }
            }
        } else {
            if (const auto debris =
                    nearest_material(agent.position, MaterialId::Debris, 1)) {
                return EntityCommand{
                    .target_tick = target_tick,
                    .issuer = agent.id,
                    .type = CommandType::Dig,
                    .target = *debris,
                    .material = MaterialId::Debris,
                };
            }
            if (const auto debris =
                    nearest_material(agent.position, MaterialId::Debris, 40)) {
                return plan_move_toward(agent, *debris, AgentAction::Move);
            }
        }
        break;
    }
    return plan_wander(agent);
}

std::optional<EntityCommand> LivingSimulation::plan_move_toward(
    const Agent& agent,
    Vec2i target,
    AgentAction action) const {
    const auto horizontal = sign(target.x - agent.position.x);
    const auto vertical = sign(target.y - agent.position.y);
    std::array<Vec2i, 4> candidates{{
        {agent.position.x + horizontal, agent.position.y},
        {agent.position.x + horizontal, agent.position.y - 1},
        {agent.position.x, agent.position.y + vertical},
        {agent.position.x - horizontal, agent.position.y},
    }};
    if (horizontal == 0) {
        std::swap(candidates[0], candidates[2]);
    }

    for (const auto candidate : candidates) {
        if (candidate == agent.position || !can_occupy(candidate) ||
            occupied(candidate, agent.id)) {
            continue;
        }
        return EntityCommand{
            .target_tick = world_.current_tick() + 1U,
            .issuer = agent.id,
            .type = CommandType::Move,
            .target = candidate,
            .action = action,
        };
    }
    return std::nullopt;
}

std::optional<EntityCommand> LivingSimulation::plan_wander(const Agent& agent) const {
    const auto direction =
        (agent_noise(agent, 0x57414E444552ULL) & 1U) == 0U ? -1 : 1;
    const Vec2i target{agent.position.x + direction, agent.position.y};
    if (!can_occupy(target) || occupied(target, agent.id)) {
        return std::nullopt;
    }
    return EntityCommand{
        .target_tick = world_.current_tick() + 1U,
        .issuer = agent.id,
        .type = CommandType::Move,
        .target = target,
    };
}

std::optional<Vec2i> LivingSimulation::reproduction_position(const Agent& agent) const {
    const auto start =
        static_cast<std::size_t>(agent_noise(agent, 0x4E455354ULL) % adjacent_directions.size());
    for (std::size_t offset = 0; offset < adjacent_directions.size(); ++offset) {
        const auto direction =
            adjacent_directions[(start + offset) % adjacent_directions.size()];
        const Vec2i candidate{
            agent.position.x + direction.x,
            agent.position.y + direction.y,
        };
        if (can_occupy(candidate) && !occupied(candidate, agent.id)) {
            return candidate;
        }
    }
    return std::nullopt;
}

void LivingSimulation::update_needs(Agent& agent) {
    ++agent.age;
    const auto hunger_rate = agent.kind == AgentKind::Predator ? 2U : 1U;
    agent.hunger = static_cast<std::uint16_t>(
        std::min<std::uint32_t>(1'500U, agent.hunger + hunger_rate));
    agent.thirst =
        static_cast<std::uint16_t>(std::min<std::uint32_t>(1'500U, agent.thirst + 1U));

    const auto material_id = world_.material(agent.position);
    if (material_id == MaterialId::Fire || material_id == MaterialId::Lava ||
        material_id == MaterialId::Acid || material_id == MaterialId::Electricity) {
        agent.health = agent.health > 8U ? static_cast<std::uint16_t>(agent.health - 8U) : 0U;
    }
    if ((agent.hunger >= 1'400U || agent.thirst >= 1'400U) &&
        world_.current_tick() % 15U == 0U) {
        agent.health = agent.health > 0U ? static_cast<std::uint16_t>(agent.health - 1U) : 0U;
    }
    agent.action = AgentAction::Idle;
}

bool LivingSimulation::apply_command(const EntityCommand& command, LivingStats& stats) {
    if (command.type == CommandType::Paint && command.issuer == world_issuer) {
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
