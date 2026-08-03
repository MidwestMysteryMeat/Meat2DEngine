#include "meat2d/scene/Scene.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <type_traits>
#include <utility>

namespace meat2d::scene {
namespace {

constexpr std::uint64_t fnv_offset = 14695981039346656037ULL;
constexpr std::uint64_t fnv_prime = 1099511628211ULL;
constexpr std::uint32_t maximum_text_bytes = 1024U * 1024U;
constexpr std::uint32_t maximum_entities = 1'000'000U;

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

void hash_text(std::uint64_t& hash, std::string_view text) noexcept {
    hash_integer(hash, static_cast<std::uint64_t>(text.size()));
    for (const auto character : text) {
        hash_byte(hash, static_cast<std::uint8_t>(character));
    }
}

void hash_rect(std::uint64_t& hash, RectI rect) noexcept {
    hash_integer(hash, rect.x);
    hash_integer(hash, rect.y);
    hash_integer(hash, rect.width);
    hash_integer(hash, rect.height);
}

void append_u8(std::vector<std::uint8_t>& bytes, std::uint8_t value) {
    bytes.push_back(value);
}

void append_u16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (std::size_t offset = 0; offset < sizeof(value); ++offset) {
        bytes.push_back(static_cast<std::uint8_t>(value >> (offset * 8U)));
    }
}

void append_i16(std::vector<std::uint8_t>& bytes, std::int16_t value) {
    append_u16(bytes, std::bit_cast<std::uint16_t>(value));
}

void append_i32(std::vector<std::uint8_t>& bytes, std::int32_t value) {
    append_u32(bytes, std::bit_cast<std::uint32_t>(value));
}

void append_text(std::vector<std::uint8_t>& bytes, std::string_view text) {
    append_u32(bytes, static_cast<std::uint32_t>(text.size()));
    bytes.insert(bytes.end(), text.begin(), text.end());
}

void append_rect(std::vector<std::uint8_t>& bytes, RectI rect) {
    append_i32(bytes, rect.x);
    append_i32(bytes, rect.y);
    append_i32(bytes, rect.width);
    append_i32(bytes, rect.height);
}

class Reader {
  public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] bool read_u8(std::uint8_t& value) noexcept {
        if (remaining() < 1U) {
            return false;
        }
        value = bytes_[offset_++];
        return true;
    }

    [[nodiscard]] bool read_u16(std::uint16_t& value) noexcept {
        if (remaining() < 2U) {
            return false;
        }
        value = static_cast<std::uint16_t>(bytes_[offset_]) |
                static_cast<std::uint16_t>(bytes_[offset_ + 1U] << 8U);
        offset_ += 2U;
        return true;
    }

    [[nodiscard]] bool read_u32(std::uint32_t& value) noexcept {
        if (remaining() < 4U) {
            return false;
        }
        value = static_cast<std::uint32_t>(bytes_[offset_]) |
                (static_cast<std::uint32_t>(bytes_[offset_ + 1U]) << 8U) |
                (static_cast<std::uint32_t>(bytes_[offset_ + 2U]) << 16U) |
                (static_cast<std::uint32_t>(bytes_[offset_ + 3U]) << 24U);
        offset_ += 4U;
        return true;
    }

    [[nodiscard]] bool read_i16(std::int16_t& value) noexcept {
        std::uint16_t encoded{};
        if (!read_u16(encoded)) {
            return false;
        }
        value = std::bit_cast<std::int16_t>(encoded);
        return true;
    }

    [[nodiscard]] bool read_i32(std::int32_t& value) noexcept {
        std::uint32_t encoded{};
        if (!read_u32(encoded)) {
            return false;
        }
        value = std::bit_cast<std::int32_t>(encoded);
        return true;
    }

    [[nodiscard]] bool read_text(std::string& value) {
        std::uint32_t length{};
        if (!read_u32(length) || length > maximum_text_bytes || remaining() < length) {
            return false;
        }
        if (length == 0U) {
            value.clear();
        } else {
            value.assign(reinterpret_cast<const char*>(bytes_.data() + offset_), length);
        }
        offset_ += length;
        return true;
    }

    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - offset_;
    }

  private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_{};
};

bool read_rect(Reader& reader, RectI& rect) noexcept {
    return reader.read_i32(rect.x) && reader.read_i32(rect.y) &&
           reader.read_i32(rect.width) && reader.read_i32(rect.height);
}

} // namespace

Scene::Scene(std::string name) : name_(std::move(name)) {}

void Scene::clear() noexcept {
    entities_.clear();
    next_entity_id_ = 1;
}

EntityId Scene::create_entity(std::string name) {
    if (next_entity_id_ == invalid_entity ||
        next_entity_id_ == std::numeric_limits<EntityId>::max()) {
        return invalid_entity;
    }
    const auto id = next_entity_id_++;
    entities_.push_back(Entity{
        .id = id,
        .name = std::move(name),
        .enabled = true,
        .transform = std::nullopt,
        .sprite = std::nullopt,
        .collider = std::nullopt,
        .rigid_body = std::nullopt,
    });
    return id;
}

bool Scene::destroy_entity(EntityId id) noexcept {
    const auto iterator = std::find_if(
        entities_.begin(), entities_.end(), [id](const Entity& entity) { return entity.id == id; });
    if (iterator == entities_.end()) {
        return false;
    }
    entities_.erase(iterator);
    return true;
}

Entity* Scene::find(EntityId id) noexcept {
    const auto iterator = std::find_if(
        entities_.begin(), entities_.end(), [id](const Entity& entity) { return entity.id == id; });
    return iterator == entities_.end() ? nullptr : &*iterator;
}

const Entity* Scene::find(EntityId id) const noexcept {
    const auto iterator = std::find_if(
        entities_.begin(), entities_.end(), [id](const Entity& entity) { return entity.id == id; });
    return iterator == entities_.end() ? nullptr : &*iterator;
}

bool Scene::contains(EntityId id) const noexcept {
    return find(id) != nullptr;
}

std::span<Entity> Scene::entities() noexcept {
    return std::span<Entity>(entities_);
}

std::span<const Entity> Scene::entities() const noexcept {
    return std::span<const Entity>(entities_);
}

Transform* Scene::add_transform(EntityId id, Transform value) noexcept {
    auto* entity = find(id);
    if (entity == nullptr) {
        return nullptr;
    }
    entity->transform = value;
    return &*entity->transform;
}

Sprite* Scene::add_sprite(EntityId id, Sprite value) noexcept {
    auto* entity = find(id);
    if (entity == nullptr) {
        return nullptr;
    }
    entity->sprite = value;
    return &*entity->sprite;
}

Collider* Scene::add_collider(EntityId id, Collider value) noexcept {
    auto* entity = find(id);
    if (entity == nullptr) {
        return nullptr;
    }
    entity->collider = value;
    return &*entity->collider;
}

RigidBody* Scene::add_rigid_body(EntityId id, RigidBody value) noexcept {
    auto* entity = find(id);
    if (entity == nullptr) {
        return nullptr;
    }
    entity->rigid_body = value;
    return &*entity->rigid_body;
}

bool Scene::remove_transform(EntityId id) noexcept {
    auto* entity = find(id);
    if (entity == nullptr || !entity->transform) {
        return false;
    }
    entity->transform.reset();
    return true;
}

bool Scene::remove_sprite(EntityId id) noexcept {
    auto* entity = find(id);
    if (entity == nullptr || !entity->sprite) {
        return false;
    }
    entity->sprite.reset();
    return true;
}

bool Scene::remove_collider(EntityId id) noexcept {
    auto* entity = find(id);
    if (entity == nullptr || !entity->collider) {
        return false;
    }
    entity->collider.reset();
    return true;
}

bool Scene::remove_rigid_body(EntityId id) noexcept {
    auto* entity = find(id);
    if (entity == nullptr || !entity->rigid_body) {
        return false;
    }
    entity->rigid_body.reset();
    return true;
}

std::optional<RectI> Scene::world_collider_bounds(EntityId id) const noexcept {
    const auto* entity = find(id);
    if (entity == nullptr || !entity->collider) {
        return std::nullopt;
    }
    auto bounds = entity->collider->bounds;
    if (entity->transform) {
        bounds.x += entity->transform->position.x;
        bounds.y += entity->transform->position.y;
    }
    return bounds;
}

std::vector<EntityId> Scene::query_colliders(RectI area, bool include_sensors) const {
    std::vector<EntityId> result;
    if (area.empty()) {
        return result;
    }
    for (const auto& entity : entities_) {
        if (!include_sensors && entity.collider && entity.collider->sensor) {
            continue;
        }
        const auto bounds = world_collider_bounds(entity.id);
        if (!bounds || bounds->empty()) {
            continue;
        }
        const auto overlaps = static_cast<std::int64_t>(bounds->x) <
                                  static_cast<std::int64_t>(area.x) + area.width &&
                              static_cast<std::int64_t>(area.x) <
                                  static_cast<std::int64_t>(bounds->x) + bounds->width &&
                              static_cast<std::int64_t>(bounds->y) <
                                  static_cast<std::int64_t>(area.y) + area.height &&
                              static_cast<std::int64_t>(area.y) <
                                  static_cast<std::int64_t>(bounds->y) + bounds->height;
        if (overlaps) {
            result.push_back(entity.id);
        }
    }
    return result;
}

const std::string& Scene::name() const noexcept {
    return name_;
}

void Scene::set_name(std::string name) {
    name_ = std::move(name);
}

std::uint64_t Scene::state_hash() const noexcept {
    std::uint64_t hash = fnv_offset;
    hash_text(hash, name_);
    hash_integer(hash, next_entity_id_);
    hash_integer(hash, static_cast<std::uint64_t>(entities_.size()));
    for (const auto& entity : entities_) {
        hash_integer(hash, entity.id);
        hash_text(hash, entity.name);
        hash_byte(hash, static_cast<std::uint8_t>(entity.enabled));

        hash_byte(hash, static_cast<std::uint8_t>(entity.transform.has_value()));
        if (entity.transform) {
            hash_integer(hash, entity.transform->position.x);
            hash_integer(hash, entity.transform->position.y);
            hash_integer(hash, entity.transform->scale.x);
            hash_integer(hash, entity.transform->scale.y);
        }

        hash_byte(hash, static_cast<std::uint8_t>(entity.sprite.has_value()));
        if (entity.sprite) {
            hash_integer(hash, entity.sprite->asset_id);
            hash_rect(hash, entity.sprite->source);
            hash_integer(hash, entity.sprite->layer);
            hash_byte(hash, static_cast<std::uint8_t>(entity.sprite->visible));
        }

        hash_byte(hash, static_cast<std::uint8_t>(entity.collider.has_value()));
        if (entity.collider) {
            hash_byte(hash, static_cast<std::uint8_t>(entity.collider->shape));
            hash_rect(hash, entity.collider->bounds);
            hash_byte(hash, static_cast<std::uint8_t>(entity.collider->sensor));
            hash_integer(hash, entity.collider->category_bits);
            hash_integer(hash, entity.collider->mask_bits);
        }

        hash_byte(hash, static_cast<std::uint8_t>(entity.rigid_body.has_value()));
        if (entity.rigid_body) {
            hash_integer(hash, entity.rigid_body->velocity.x);
            hash_integer(hash, entity.rigid_body->velocity.y);
            hash_integer(hash, entity.rigid_body->acceleration.x);
            hash_integer(hash, entity.rigid_body->acceleration.y);
            hash_integer(hash, entity.rigid_body->max_velocity.x);
            hash_integer(hash, entity.rigid_body->max_velocity.y);
            hash_byte(hash, static_cast<std::uint8_t>(entity.rigid_body->dynamic));
            hash_byte(hash, static_cast<std::uint8_t>(entity.rigid_body->affected_by_gravity));
        }
    }
    return hash;
}

std::vector<std::uint8_t> Scene::serialize() const {
    if (name_.size() > maximum_text_bytes || entities_.size() > maximum_entities ||
        entities_.size() > std::numeric_limits<std::uint32_t>::max()) {
        return {};
    }
    for (const auto& entity : entities_) {
        if (entity.name.size() > maximum_text_bytes) {
            return {};
        }
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(32U + name_.size() + entities_.size() * 64U);
    append_u8(bytes, 'M');
    append_u8(bytes, '2');
    append_u8(bytes, 'S');
    append_u8(bytes, 'C');
    append_u16(bytes, scene_format_version);
    append_text(bytes, name_);
    append_u32(bytes, next_entity_id_);
    append_u32(bytes, static_cast<std::uint32_t>(entities_.size()));
    for (const auto& entity : entities_) {
        append_u32(bytes, entity.id);
        append_u8(bytes, static_cast<std::uint8_t>(entity.enabled));
        append_text(bytes, entity.name);
        const auto flags = static_cast<std::uint8_t>(
            static_cast<std::uint8_t>(entity.transform.has_value()) |
            static_cast<std::uint8_t>(entity.sprite.has_value() << 1U) |
            static_cast<std::uint8_t>(entity.collider.has_value() << 2U) |
            static_cast<std::uint8_t>(entity.rigid_body.has_value() << 3U));
        append_u8(bytes, flags);
        if (entity.transform) {
            append_i32(bytes, entity.transform->position.x);
            append_i32(bytes, entity.transform->position.y);
            append_i32(bytes, entity.transform->scale.x);
            append_i32(bytes, entity.transform->scale.y);
        }
        if (entity.sprite) {
            append_u32(bytes, entity.sprite->asset_id);
            append_rect(bytes, entity.sprite->source);
            append_i16(bytes, entity.sprite->layer);
            append_u8(bytes, static_cast<std::uint8_t>(entity.sprite->visible));
        }
        if (entity.collider) {
            append_u8(bytes, static_cast<std::uint8_t>(entity.collider->shape));
            append_rect(bytes, entity.collider->bounds);
            append_u8(bytes, static_cast<std::uint8_t>(entity.collider->sensor));
            append_u16(bytes, entity.collider->category_bits);
            append_u16(bytes, entity.collider->mask_bits);
        }
        if (entity.rigid_body) {
            append_i32(bytes, entity.rigid_body->velocity.x);
            append_i32(bytes, entity.rigid_body->velocity.y);
            append_i32(bytes, entity.rigid_body->acceleration.x);
            append_i32(bytes, entity.rigid_body->acceleration.y);
            append_i32(bytes, entity.rigid_body->max_velocity.x);
            append_i32(bytes, entity.rigid_body->max_velocity.y);
            append_u8(bytes, static_cast<std::uint8_t>(entity.rigid_body->dynamic));
            append_u8(bytes, static_cast<std::uint8_t>(entity.rigid_body->affected_by_gravity));
        }
    }
    return bytes;
}

std::optional<Scene> Scene::deserialize(std::span<const std::uint8_t> bytes) {
    Reader reader(bytes);
    std::uint8_t magic[4]{};
    for (auto& value : magic) {
        if (!reader.read_u8(value)) {
            return std::nullopt;
        }
    }
    if (magic[0] != 'M' || magic[1] != '2' || magic[2] != 'S' || magic[3] != 'C') {
        return std::nullopt;
    }

    std::uint16_t version{};
    if (!reader.read_u16(version) || version != scene_format_version) {
        return std::nullopt;
    }
    Scene result;
    if (!reader.read_text(result.name_)) {
        return std::nullopt;
    }
    std::uint32_t next_entity_id{};
    std::uint32_t entity_count{};
    if (!reader.read_u32(next_entity_id) || !reader.read_u32(entity_count) ||
        next_entity_id == invalid_entity || entity_count > maximum_entities ||
        entity_count > reader.remaining()) {
        return std::nullopt;
    }
    result.next_entity_id_ = next_entity_id;
    result.entities_.reserve(entity_count);
    EntityId previous_id{};
    for (std::uint32_t index = 0; index < entity_count; ++index) {
        Entity entity;
        std::uint8_t enabled{};
        std::uint8_t flags{};
        if (!reader.read_u32(entity.id) || entity.id == invalid_entity || entity.id <= previous_id ||
            !reader.read_u8(enabled) || enabled > 1U || !reader.read_text(entity.name) ||
            !reader.read_u8(flags) || (flags & 0xF0U) != 0U) {
            return std::nullopt;
        }
        entity.enabled = enabled != 0U;
        if ((flags & 0x01U) != 0U) {
            Transform transform;
            if (!reader.read_i32(transform.position.x) || !reader.read_i32(transform.position.y) ||
                !reader.read_i32(transform.scale.x) || !reader.read_i32(transform.scale.y)) {
                return std::nullopt;
            }
            entity.transform = transform;
        }
        if ((flags & 0x02U) != 0U) {
            Sprite sprite;
            std::uint8_t visible{};
            if (!reader.read_u32(sprite.asset_id) || !read_rect(reader, sprite.source) ||
                !reader.read_i16(sprite.layer) || !reader.read_u8(visible) || visible > 1U) {
                return std::nullopt;
            }
            sprite.visible = visible != 0U;
            entity.sprite = sprite;
        }
        if ((flags & 0x04U) != 0U) {
            Collider collider;
            std::uint8_t shape{};
            std::uint8_t sensor{};
            if (!reader.read_u8(shape) || shape > static_cast<std::uint8_t>(ColliderShape::Circle) ||
                !read_rect(reader, collider.bounds) || !reader.read_u8(sensor) || sensor > 1U ||
                !reader.read_u16(collider.category_bits) || !reader.read_u16(collider.mask_bits)) {
                return std::nullopt;
            }
            collider.shape = static_cast<ColliderShape>(shape);
            collider.sensor = sensor != 0U;
            entity.collider = collider;
        }
        if ((flags & 0x08U) != 0U) {
            RigidBody body;
            std::uint8_t dynamic{};
            std::uint8_t affected_by_gravity{};
            if (!reader.read_i32(body.velocity.x) || !reader.read_i32(body.velocity.y) ||
                !reader.read_i32(body.acceleration.x) || !reader.read_i32(body.acceleration.y) ||
                !reader.read_i32(body.max_velocity.x) || !reader.read_i32(body.max_velocity.y) ||
                !reader.read_u8(dynamic) || dynamic > 1U ||
                !reader.read_u8(affected_by_gravity) || affected_by_gravity > 1U) {
                return std::nullopt;
            }
            body.dynamic = dynamic != 0U;
            body.affected_by_gravity = affected_by_gravity != 0U;
            entity.rigid_body = body;
        }
        previous_id = entity.id;
        result.entities_.push_back(std::move(entity));
    }
    if (reader.remaining() != 0U ||
        (previous_id != 0U && result.next_entity_id_ <= previous_id)) {
        return std::nullopt;
    }
    return result;
}

} // namespace meat2d::scene
