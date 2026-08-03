#pragma once

#include "meat2d/core/Types.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace meat2d::scene {

using EntityId = std::uint32_t;
inline constexpr EntityId invalid_entity = 0;
inline constexpr std::uint16_t scene_format_version = 4;
inline constexpr std::uint16_t minimum_supported_scene_format_version = 3;

struct Transform {
    Vec2i position{};
    Vec2i scale{1, 1};
};

struct Sprite {
    std::uint32_t asset_id{};
    RectI source{};
    std::int16_t layer{};
    bool visible{true};
};

enum class ColliderShape : std::uint8_t { Box, Circle };

struct Collider {
    ColliderShape shape{ColliderShape::Box};
    // Bounds are local to the entity transform when a transform exists.
    RectI bounds{};
    bool sensor{};
    std::uint16_t category_bits{1};
    std::uint16_t mask_bits{0xFFFF};
};

struct RigidBody {
    Vec2i velocity{};
    Vec2i acceleration{};
    Vec2i max_velocity{32, 32};
    bool dynamic{true};
    bool affected_by_gravity{true};
};

// A scene entity is intentionally small and data-oriented at this stage. The
// cellular World remains a separate authoritative simulation; these components
// are for ordinary game actors, presentation, and future replication.
struct Entity {
    EntityId id{};
    std::string name;
    bool enabled{true};
    EntityId parent{invalid_entity};
    std::vector<std::string> tags;
    std::optional<Transform> transform;
    std::optional<Sprite> sprite;
    std::optional<Collider> collider;
    std::optional<RigidBody> rigid_body;
};

class Scene {
  public:
    explicit Scene(std::string name = {});

    void clear() noexcept;

    [[nodiscard]] EntityId create_entity(std::string name = {});
    bool destroy_entity(EntityId id) noexcept;

    [[nodiscard]] Entity* find(EntityId id) noexcept;
    [[nodiscard]] const Entity* find(EntityId id) const noexcept;
    [[nodiscard]] bool contains(EntityId id) const noexcept;

    // Parent transforms use local integer positions. A parent may be changed
    // only when it exists and would not introduce a cycle. Scale inheritance
    // is intentionally deferred until the renderer has a matching contract.
    bool set_parent(EntityId child, EntityId parent) noexcept;
    [[nodiscard]] EntityId parent_of(EntityId child) const noexcept;
    [[nodiscard]] Vec2i world_position(EntityId id) const noexcept;

    bool add_tag(EntityId id, std::string tag);
    bool remove_tag(EntityId id, std::string_view tag) noexcept;
    [[nodiscard]] bool has_tag(EntityId id, std::string_view tag) const noexcept;
    [[nodiscard]] std::vector<EntityId> find_tagged(std::string_view tag) const;

    [[nodiscard]] std::span<Entity> entities() noexcept;
    [[nodiscard]] std::span<const Entity> entities() const noexcept;

    [[nodiscard]] Transform* add_transform(EntityId id, Transform value = {}) noexcept;
    [[nodiscard]] Sprite* add_sprite(EntityId id, Sprite value = {}) noexcept;
    [[nodiscard]] Collider* add_collider(EntityId id, Collider value = {}) noexcept;
    [[nodiscard]] RigidBody* add_rigid_body(EntityId id, RigidBody value = {}) noexcept;
    bool remove_transform(EntityId id) noexcept;
    bool remove_sprite(EntityId id) noexcept;
    bool remove_collider(EntityId id) noexcept;
    bool remove_rigid_body(EntityId id) noexcept;

    [[nodiscard]] std::optional<RectI> world_collider_bounds(EntityId id) const noexcept;
    [[nodiscard]] std::vector<EntityId> query_colliders(RectI area,
                                                        bool include_sensors = true) const;

    [[nodiscard]] const std::string& name() const noexcept;
    void set_name(std::string name);

    // Hashes serialized field values in entity-ID order. It deliberately does
    // not hash object addresses or structure padding, so equivalent scenes
    // created independently produce the same result.
    [[nodiscard]] std::uint64_t state_hash() const noexcept;

    // Encodes a versioned, little-endian scene document. The format is kept
    // independent from the network protocol so editor files can evolve on a
    // separate compatibility schedule.
    [[nodiscard]] std::vector<std::uint8_t> serialize() const;
    [[nodiscard]] static std::optional<Scene> deserialize(std::span<const std::uint8_t> bytes);

  private:
    [[nodiscard]] bool hierarchy_valid() const noexcept;

    std::string name_;
    std::vector<Entity> entities_;
    EntityId next_entity_id_{1};
};

} // namespace meat2d::scene
