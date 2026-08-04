#include "meat2d/scene/Scene.hpp"

#include <bit>
#include <cstddef>
#include <limits>
#include <utility>

namespace meat2d::scene {
namespace {

constexpr std::uint32_t maximum_text_bytes = 1024U * 1024U;
constexpr std::uint32_t maximum_entities = 1'000'000U;
constexpr std::uint32_t maximum_tags_per_entity = 256U;

bool valid_tag_text(std::string_view tag) noexcept {
    return !tag.empty() && tag.size() <= maximum_text_bytes;
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

std::vector<std::uint8_t> Scene::serialize() const {
    if (name_.size() > maximum_text_bytes || entities_.size() > maximum_entities ||
        entities_.size() > std::numeric_limits<std::uint32_t>::max()) {
        return {};
    }
    for (const auto& entity : entities_) {
        if (entity.name.size() > maximum_text_bytes || entity.tags.size() > maximum_tags_per_entity ||
            entity.parent == entity.id ||
            (entity.parent != invalid_entity && !contains(entity.parent))) {
            return {};
        }
        for (const auto& tag : entity.tags) {
            if (!valid_tag_text(tag)) {
                return {};
            }
        }
    }
    if (!hierarchy_valid()) {
        return {};
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
        append_u32(bytes, entity.parent);
        append_u32(bytes, static_cast<std::uint32_t>(entity.tags.size()));
        for (const auto& tag : entity.tags) {
            append_text(bytes, tag);
        }
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
    if (!reader.read_u16(version) ||
        (version != minimum_supported_scene_format_version && version != scene_format_version)) {
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
        std::uint32_t tag_count{};
        if (!reader.read_u32(entity.id) || entity.id == invalid_entity || entity.id <= previous_id ||
            !reader.read_u8(enabled) || enabled > 1U || !reader.read_text(entity.name) ||
            (version == scene_format_version && !reader.read_u32(entity.parent)) ||
            (version == scene_format_version &&
             (!reader.read_u32(tag_count) || tag_count > maximum_tags_per_entity))) {
            return std::nullopt;
        }
        entity.enabled = enabled != 0U;
        if (version == scene_format_version) {
            entity.tags.reserve(tag_count);
            std::string previous_tag;
            for (std::uint32_t tag_index = 0; tag_index < tag_count; ++tag_index) {
                std::string tag;
                if (!reader.read_text(tag) || !valid_tag_text(tag) ||
                    (!previous_tag.empty() && tag <= previous_tag)) {
                    return std::nullopt;
                }
                entity.tags.push_back(std::move(tag));
                previous_tag = entity.tags.back();
            }
        }
        if (!reader.read_u8(flags) || (flags & 0xF0U) != 0U) {
            return std::nullopt;
        }
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
        (previous_id != 0U && result.next_entity_id_ <= previous_id) || !result.hierarchy_valid()) {
        return std::nullopt;
    }
    return result;
}

} // namespace meat2d::scene
