#include "meat2d/sim/SessionSnapshot.hpp"

#include "meat2d/scene/SceneSnapshot.hpp"
#include "meat2d/sim/WorldSnapshot.hpp"

#include <array>
#include <algorithm>
#include <limits>
#include <utility>

namespace meat2d::persistence {
namespace {

constexpr std::array<std::uint8_t, 4> session_magic{{'M', '2', 'S', 'S'}};
constexpr std::size_t encoded_header_bytes = 4U + 2U + 8U + 4U + 4U + 8U + 8U;

void append_u16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (std::size_t offset = 0; offset < sizeof(value); ++offset) {
        bytes.push_back(static_cast<std::uint8_t>(value >> (offset * 8U)));
    }
}

void append_u64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (std::size_t offset = 0; offset < sizeof(value); ++offset) {
        bytes.push_back(static_cast<std::uint8_t>(value >> (offset * 8U)));
    }
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

    [[nodiscard]] bool read_u64(std::uint64_t& value) noexcept {
        if (remaining() < 8U) {
            return false;
        }
        value = 0;
        for (std::size_t offset = 0; offset < sizeof(value); ++offset) {
            value |= static_cast<std::uint64_t>(bytes_[offset_ + offset]) << (offset * 8U);
        }
        offset_ += 8U;
        return true;
    }

    [[nodiscard]] std::span<const std::uint8_t> read_bytes(std::size_t count) noexcept {
        if (count > remaining()) {
            return {};
        }
        const auto result = bytes_.subspan(offset_, count);
        offset_ += count;
        return result;
    }

    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - offset_;
    }

  private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_{};
};

} // namespace

std::vector<std::uint8_t> encode_session(std::uint64_t session_id, const World& world,
                                         const scene::Scene& scene, std::size_t maximum_bytes) {
    if (maximum_bytes < encoded_header_bytes) {
        return {};
    }
    const auto component_budget = maximum_bytes - encoded_header_bytes;
    auto world_bytes = encode_world(world, component_budget);
    const auto scene_snapshot = scene::capture_snapshot(
        scene, std::min(component_budget, scene::maximum_scene_snapshot_bytes));
    if (world_bytes.empty() || !scene_snapshot) {
        return {};
    }
    const auto& scene_bytes = scene_snapshot->bytes;
    const auto total = encoded_header_bytes + world_bytes.size() + scene_bytes.size();
    if (total > maximum_bytes || world_bytes.size() > std::numeric_limits<std::uint32_t>::max() ||
        scene_bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
        return {};
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(total);
    bytes.insert(bytes.end(), session_magic.begin(), session_magic.end());
    append_u16(bytes, session_snapshot_format_version);
    append_u64(bytes, session_id);
    append_u32(bytes, static_cast<std::uint32_t>(world_bytes.size()));
    append_u32(bytes, static_cast<std::uint32_t>(scene_bytes.size()));
    append_u64(bytes, world.state_hash());
    append_u64(bytes, scene_snapshot->state_hash);
    bytes.insert(bytes.end(), world_bytes.begin(), world_bytes.end());
    bytes.insert(bytes.end(), scene_bytes.begin(), scene_bytes.end());
    return bytes;
}

std::optional<RestoredSession> decode_session(std::span<const std::uint8_t> bytes,
                                               std::size_t maximum_bytes) {
    if (bytes.size() < encoded_header_bytes || bytes.size() > maximum_bytes) {
        return std::nullopt;
    }
    Reader reader(bytes);
    std::array<std::uint8_t, session_magic.size()> magic{};
    for (auto& value : magic) {
        if (!reader.read_u8(value)) {
            return std::nullopt;
        }
    }
    if (magic != session_magic) {
        return std::nullopt;
    }

    std::uint16_t version{};
    std::uint64_t session_id{};
    std::uint32_t world_size{};
    std::uint32_t scene_size{};
    std::uint64_t world_hash{};
    std::uint64_t scene_hash{};
    if (!reader.read_u16(version) || version != session_snapshot_format_version ||
        !reader.read_u64(session_id) || !reader.read_u32(world_size) ||
        !reader.read_u32(scene_size) || !reader.read_u64(world_hash) ||
        !reader.read_u64(scene_hash) ||
        scene_size > scene::maximum_scene_snapshot_bytes ||
        static_cast<std::uint64_t>(world_size) + static_cast<std::uint64_t>(scene_size) !=
            reader.remaining()) {
        return std::nullopt;
    }
    const auto world_bytes = reader.read_bytes(world_size);
    const auto scene_bytes = reader.read_bytes(scene_size);
    if (world_bytes.empty() || scene_bytes.empty() || reader.remaining() != 0U) {
        return std::nullopt;
    }

    auto world = decode_world(world_bytes);
    auto scene = scene::Scene::deserialize(scene_bytes);
    if (!world || !scene || world->state_hash() != world_hash || scene->state_hash() != scene_hash) {
        return std::nullopt;
    }
    return RestoredSession{
        .session_id = session_id,
        .world = std::move(*world),
        .scene = std::move(*scene),
    };
}

} // namespace meat2d::persistence
