#include "meat2d/replay/Replay.hpp"

#include <algorithm>
#include <array>

namespace meat2d::replay {
namespace {

constexpr std::array<std::uint8_t, 4> file_magic{{'M', '2', 'R', 'P'}};
constexpr std::uint8_t file_version = 1;

void push_u8(std::vector<std::uint8_t>& bytes, std::uint8_t value) {
    bytes.push_back(value);
}

void push_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        push_u8(bytes, static_cast<std::uint8_t>(value >> shift));
    }
}

void push_u64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        push_u8(bytes, static_cast<std::uint8_t>(value >> shift));
    }
}

void push_i32(std::vector<std::uint8_t>& bytes, std::int32_t value) {
    push_u32(bytes, static_cast<std::uint32_t>(value));
}

class Cursor {
  public:
    explicit Cursor(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    bool read_u8(std::uint8_t& value) {
        if (offset_ >= bytes_.size()) {
            return false;
        }
        value = bytes_[offset_++];
        return true;
    }

    bool read_u32(std::uint32_t& value) {
        value = 0;
        for (int shift = 0; shift < 32; shift += 8) {
            std::uint8_t byte = 0;
            if (!read_u8(byte)) {
                return false;
            }
            value |= static_cast<std::uint32_t>(byte) << shift;
        }
        return true;
    }

    bool read_u64(std::uint64_t& value) {
        value = 0;
        for (int shift = 0; shift < 64; shift += 8) {
            std::uint8_t byte = 0;
            if (!read_u8(byte)) {
                return false;
            }
            value |= static_cast<std::uint64_t>(byte) << shift;
        }
        return true;
    }

    bool read_i32(std::int32_t& value) {
        std::uint32_t encoded = 0;
        if (!read_u32(encoded)) {
            return false;
        }
        value = static_cast<std::int32_t>(encoded);
        return true;
    }

  private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_{};
};

} // namespace

ReplayLog::ReplayLog(WorldConfig config) : config_(config) {}

void ReplayLog::record_paint(Tick tick, Vec2i position, std::int32_t radius, MaterialId material) {
    paint_events_.push_back(PaintEvent{tick, position, radius, material});
}

void ReplayLog::record_checkpoint(Tick tick, std::uint64_t state_hash) {
    checkpoints_.push_back(Checkpoint{tick, state_hash});
}

const WorldConfig& ReplayLog::config() const noexcept {
    return config_;
}

std::span<const PaintEvent> ReplayLog::paint_events() const noexcept {
    return paint_events_;
}

std::span<const Checkpoint> ReplayLog::checkpoints() const noexcept {
    return checkpoints_;
}

std::vector<std::uint8_t> ReplayLog::encode() const {
    std::vector<std::uint8_t> bytes;
    bytes.insert(bytes.end(), file_magic.begin(), file_magic.end());
    push_u8(bytes, file_version);

    push_i32(bytes, config_.width);
    push_i32(bytes, config_.height);
    push_u64(bytes, config_.seed);
    push_u32(bytes, config_.sleep_after_ticks);

    push_u32(bytes, static_cast<std::uint32_t>(paint_events_.size()));
    for (const auto& event : paint_events_) {
        push_u64(bytes, event.tick);
        push_i32(bytes, event.position.x);
        push_i32(bytes, event.position.y);
        push_i32(bytes, event.radius);
        push_u8(bytes, static_cast<std::uint8_t>(event.material));
    }

    push_u32(bytes, static_cast<std::uint32_t>(checkpoints_.size()));
    for (const auto& checkpoint : checkpoints_) {
        push_u64(bytes, checkpoint.tick);
        push_u64(bytes, checkpoint.state_hash);
    }

    return bytes;
}

bool ReplayLog::decode(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < file_magic.size() + 1 ||
        !std::equal(file_magic.begin(), file_magic.end(), bytes.begin())) {
        return false;
    }

    Cursor cursor(bytes.subspan(file_magic.size()));
    std::uint8_t version = 0;
    if (!cursor.read_u8(version) || version != file_version) {
        return false;
    }

    WorldConfig config{};
    std::uint32_t sleep_after_ticks = 0;
    if (!cursor.read_i32(config.width) || !cursor.read_i32(config.height) ||
        !cursor.read_u64(config.seed) || !cursor.read_u32(sleep_after_ticks)) {
        return false;
    }
    config.sleep_after_ticks = static_cast<std::uint16_t>(sleep_after_ticks);

    std::uint32_t paint_count = 0;
    if (!cursor.read_u32(paint_count)) {
        return false;
    }
    std::vector<PaintEvent> paint_events;
    paint_events.reserve(paint_count);
    for (std::uint32_t index = 0; index < paint_count; ++index) {
        PaintEvent event{};
        std::uint8_t material = 0;
        if (!cursor.read_u64(event.tick) || !cursor.read_i32(event.position.x) ||
            !cursor.read_i32(event.position.y) || !cursor.read_i32(event.radius) ||
            !cursor.read_u8(material)) {
            return false;
        }
        event.material = static_cast<MaterialId>(material);
        if (!is_valid(event.material)) {
            return false;
        }
        paint_events.push_back(event);
    }

    std::uint32_t checkpoint_count = 0;
    if (!cursor.read_u32(checkpoint_count)) {
        return false;
    }
    std::vector<Checkpoint> checkpoints;
    checkpoints.reserve(checkpoint_count);
    for (std::uint32_t index = 0; index < checkpoint_count; ++index) {
        Checkpoint checkpoint{};
        if (!cursor.read_u64(checkpoint.tick) || !cursor.read_u64(checkpoint.state_hash)) {
            return false;
        }
        checkpoints.push_back(checkpoint);
    }

    config_ = config;
    paint_events_ = std::move(paint_events);
    checkpoints_ = std::move(checkpoints);
    return true;
}

ReplayResult play(const ReplayLog& log, Tick total_ticks) {
    World world(log.config());
    const auto paints = log.paint_events();
    const auto checkpoints = log.checkpoints();
    std::size_t next_paint = 0;
    std::size_t next_checkpoint = 0;

    for (Tick step_index = 0; step_index < total_ticks; ++step_index) {
        while (next_paint < paints.size() && paints[next_paint].tick == world.current_tick()) {
            const auto& event = paints[next_paint];
            world.paint_disc(event.position, event.radius, event.material);
            ++next_paint;
        }

        world.step();

        while (next_checkpoint < checkpoints.size() &&
               checkpoints[next_checkpoint].tick == world.current_tick()) {
            const auto& checkpoint = checkpoints[next_checkpoint];
            const auto actual_hash = world.state_hash();
            if (actual_hash != checkpoint.state_hash) {
                return ReplayResult{
                    ReplayOutcome::Diverged, step_index + 1,          world.current_tick(),
                    checkpoint.state_hash,   actual_hash,
                };
            }
            ++next_checkpoint;
        }
    }

    return ReplayResult{ReplayOutcome::Matched, total_ticks, 0, 0, 0};
}

} // namespace meat2d::replay
