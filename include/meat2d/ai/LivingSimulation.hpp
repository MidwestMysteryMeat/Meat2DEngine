#pragma once

#include "meat2d/core/Types.hpp"
#include "meat2d/life/OrganismField.hpp"
#include "meat2d/sim/World.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace meat2d::ai {

using EntityId = std::uint32_t;
inline constexpr EntityId world_issuer = 0;
inline constexpr std::size_t maximum_agents = 1'024;

enum class AgentKind : std::uint8_t {
    Grazer,
    Predator,
    Worker
};

enum class AgentAction : std::uint8_t {
    Idle,
    Move,
    Flee,
    Eat,
    Drink,
    Attack,
    Dig,
    Place,
    Reproduce
};

struct Agent {
    EntityId id{};
    AgentKind kind{AgentKind::Grazer};
    AgentAction action{AgentAction::Idle};
    Vec2i position{};
    std::uint32_t genome{};
    std::uint32_t age{};
    std::uint16_t health{100};
    std::uint16_t hunger{};
    std::uint16_t thirst{};
    std::uint16_t generation{};
    MaterialId carried{MaterialId::Empty};
};

enum class CommandType : std::uint8_t {
    Move,
    Consume,
    Attack,
    Dig,
    Place,
    Reproduce,
    Paint
};

struct EntityCommand {
    Tick target_tick{};
    EntityId issuer{};
    std::uint32_t sequence{};
    CommandType type{CommandType::Move};
    EntityId target_entity{};
    Vec2i target{};
    MaterialId material{MaterialId::Empty};
    AgentAction action{AgentAction::Idle};
};

struct LivingStats {
    TickStats world{};
    life::OrganismStats organisms{};
    std::uint32_t planned_commands{};
    std::uint32_t applied_commands{};
    std::uint32_t rejected_commands{};
    std::uint32_t births{};
    std::uint32_t deaths{};
};

class LivingSimulation {
  public:
    explicit LivingSimulation(WorldConfig config = {});

    [[nodiscard]] World& world() noexcept;
    [[nodiscard]] const World& world() const noexcept;
    [[nodiscard]] life::OrganismField& organisms() noexcept;
    [[nodiscard]] const life::OrganismField& organisms() const noexcept;
    [[nodiscard]] std::span<const Agent> agents() const noexcept;
    [[nodiscard]] const Agent* find_agent(EntityId id) const noexcept;

    EntityId spawn_agent(
        AgentKind kind,
        Vec2i position,
        std::uint32_t genome = 0,
        std::uint16_t generation = 0);
    bool queue_command(EntityCommand command);
    LivingStats step();

    [[nodiscard]] std::uint64_t state_hash() const noexcept;

  private:
    [[nodiscard]] Agent* find_agent_mutable(EntityId id) noexcept;
    [[nodiscard]] bool can_occupy(Vec2i position) const noexcept;
    [[nodiscard]] bool occupied(Vec2i position, EntityId ignored = 0) const noexcept;
    [[nodiscard]] bool adjacent(Vec2i first, Vec2i second) const noexcept;
    [[nodiscard]] std::optional<Vec2i> nearest_material(
        Vec2i origin,
        MaterialId material,
        std::int32_t range) const;
    [[nodiscard]] const Agent* nearest_agent(
        Vec2i origin,
        AgentKind kind,
        std::int32_t range,
        EntityId ignored) const noexcept;
    [[nodiscard]] std::optional<Vec2i> nearest_danger(
        Vec2i origin,
        std::int32_t range) const;
    [[nodiscard]] std::optional<EntityCommand> plan_agent(const Agent& agent) const;
    [[nodiscard]] std::optional<EntityCommand> plan_move_toward(
        const Agent& agent,
        Vec2i target,
        AgentAction action) const;
    [[nodiscard]] std::optional<EntityCommand> plan_wander(const Agent& agent) const;
    [[nodiscard]] std::optional<Vec2i> reproduction_position(const Agent& agent) const;
    void update_needs(Agent& agent);
    bool apply_command(const EntityCommand& command, LivingStats& stats);
    void remove_dead(LivingStats& stats);
    [[nodiscard]] std::uint64_t agent_noise(const Agent& agent, std::uint64_t salt) const noexcept;

    World world_;
    life::OrganismField organisms_;
    std::vector<Agent> agents_;
    std::vector<EntityCommand> commands_;
    EntityId next_entity_id_{1};
};

} // namespace meat2d::ai
