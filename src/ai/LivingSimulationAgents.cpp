#include "meat2d/ai/LivingSimulation.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace meat2d::ai {
namespace {

constexpr std::array<Vec2i, 8> adjacent_directions{{
    {-1, -1},
    {0, -1},
    {1, -1},
    {-1, 0},
    {1, 0},
    {-1, 1},
    {0, 1},
    {1, 1},
}};

std::int32_t absolute(std::int32_t value) noexcept {
    return value < 0 ? -value : value;
}

std::int32_t distance(Vec2i first, Vec2i second) noexcept {
    return absolute(first.x - second.x) + absolute(first.y - second.y);
}

std::int32_t sign(std::int32_t value) noexcept {
    return value < 0 ? -1 : value > 0 ? 1 : 0;
}

} // namespace

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

} // namespace meat2d::ai

