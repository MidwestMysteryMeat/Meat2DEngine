#include "meat2d/sim/WorldSnapshot.hpp"

#include <array>
#include <bit>
#include <limits>
#include <stdexcept>

namespace meat2d::persistence {
namespace {

constexpr std::array<std::uint8_t, 4> snapshot_magic{{'M', '2', 'W', 'S'}};
constexpr std::size_t encoded_cell_bytes = 8U;
constexpr std::size_t encoded_header_bytes = 4U + 2U + 4U + 4U + 8U + 2U + 8U + 4U + 4U;
constexpr std::uint64_t maximum_snapshot_cells = 64'000'000U;

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

void append_u64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (std::size_t offset = 0; offset < sizeof(value); ++offset) {
        bytes.push_back(static_cast<std::uint8_t>(value >> (offset * 8U)));
    }
}

void append_i16(std::vector<std::uint8_t>& bytes, std::int16_t value) {
    append_u16(bytes, std::bit_cast<std::uint16_t>(value));
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

    [[nodiscard]] bool read_i16(std::int16_t& value) noexcept {
        std::uint16_t encoded{};
        if (!read_u16(encoded)) {
            return false;
        }
        value = std::bit_cast<std::int16_t>(encoded);
        return true;
    }

    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - offset_;
    }

  private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_{};
};

bool valid_dimensions(std::int32_t width, std::int32_t height, std::uint16_t sleep_after_ticks,
                      std::uint64_t maximum_bytes) noexcept {
    if (width <= 0 || height <= 0 || sleep_after_ticks == 0U) {
        return false;
    }
    const auto cells = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    const auto columns =
        (static_cast<std::uint64_t>(width) + static_cast<std::uint64_t>(chunk_size) - 1U) /
        static_cast<std::uint64_t>(chunk_size);
    const auto rows =
        (static_cast<std::uint64_t>(height) + static_cast<std::uint64_t>(chunk_size) - 1U) /
        static_cast<std::uint64_t>(chunk_size);
    const auto chunk_cells = columns * rows * cells_per_chunk;
    return cells <= maximum_snapshot_cells &&
           encoded_header_bytes + chunk_cells * encoded_cell_bytes <= maximum_bytes;
}

} // namespace

std::vector<std::uint8_t> encode_world(const World& world, std::size_t maximum_bytes) {
    if (!valid_dimensions(world.width(), world.height(), world.sleep_after_ticks(), maximum_bytes)) {
        return {};
    }
    const auto chunk_count = static_cast<std::uint64_t>(world.chunk_columns()) *
                             static_cast<std::uint64_t>(world.chunk_rows());
    const auto required = encoded_header_bytes + chunk_count * cells_per_chunk * encoded_cell_bytes;
    if (chunk_count > std::numeric_limits<std::uint32_t>::max() || required > maximum_bytes ||
        required > std::numeric_limits<std::size_t>::max()) {
        return {};
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(static_cast<std::size_t>(required));
    bytes.insert(bytes.end(), snapshot_magic.begin(), snapshot_magic.end());
    append_u16(bytes, world_snapshot_format_version);
    append_u32(bytes, static_cast<std::uint32_t>(world.width()));
    append_u32(bytes, static_cast<std::uint32_t>(world.height()));
    append_u64(bytes, world.seed());
    append_u16(bytes, world.sleep_after_ticks());
    append_u64(bytes, world.current_tick());
    append_u32(bytes, static_cast<std::uint32_t>(world.chunk_columns()));
    append_u32(bytes, static_cast<std::uint32_t>(world.chunk_rows()));

    for (const auto& chunk : world.chunks()) {
        for (const auto& cell : chunk.cells) {
            append_u8(bytes, static_cast<std::uint8_t>(cell.material));
            append_u8(bytes, cell.variant);
            append_u8(bytes, cell.state);
            append_u8(bytes, 0U);
            append_i16(bytes, cell.temperature);
            append_u8(bytes, static_cast<std::uint8_t>(cell.velocity_x));
            append_u8(bytes, static_cast<std::uint8_t>(cell.velocity_y));
        }
    }
    return bytes;
}

std::optional<World> decode_world(std::span<const std::uint8_t> bytes, std::size_t maximum_bytes) {
    if (bytes.empty() || bytes.size() > maximum_bytes) {
        return std::nullopt;
    }
    Reader reader(bytes);
    std::array<std::uint8_t, snapshot_magic.size()> magic{};
    for (auto& value : magic) {
        if (!reader.read_u8(value)) {
            return std::nullopt;
        }
    }
    if (magic != snapshot_magic) {
        return std::nullopt;
    }

    std::uint16_t version{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint64_t seed{};
    std::uint16_t sleep_after_ticks{};
    std::uint64_t tick{};
    std::uint32_t chunk_columns{};
    std::uint32_t chunk_rows{};
    if (!reader.read_u16(version) || version != world_snapshot_format_version ||
        !reader.read_u32(width) || !reader.read_u32(height) || !reader.read_u64(seed) ||
        !reader.read_u16(sleep_after_ticks) || !reader.read_u64(tick) ||
        !reader.read_u32(chunk_columns) || !reader.read_u32(chunk_rows) ||
        width > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) ||
        height > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) ||
        !valid_dimensions(static_cast<std::int32_t>(width), static_cast<std::int32_t>(height),
                          sleep_after_ticks, maximum_bytes)) {
        return std::nullopt;
    }

    const WorldConfig config{
        .width = static_cast<std::int32_t>(width),
        .height = static_cast<std::int32_t>(height),
        .seed = seed,
        .sleep_after_ticks = sleep_after_ticks,
    };
    const auto expected_columns = static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(config.width) + static_cast<std::uint64_t>(chunk_size) - 1U) /
        static_cast<std::uint64_t>(chunk_size));
    const auto expected_rows = static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(config.height) + static_cast<std::uint64_t>(chunk_size) - 1U) /
        static_cast<std::uint64_t>(chunk_size));
    const auto chunk_count = static_cast<std::uint64_t>(expected_columns) * expected_rows;
    if (chunk_columns != expected_columns || chunk_rows != expected_rows ||
        reader.remaining() != chunk_count * cells_per_chunk * encoded_cell_bytes) {
        return std::nullopt;
    }

    World result(config);
    std::array<Cell, cells_per_chunk> cells{};
    for (std::uint32_t row = 0; row < chunk_rows; ++row) {
        for (std::uint32_t column = 0; column < chunk_columns; ++column) {
            for (auto& cell : cells) {
                std::uint8_t material{};
                std::uint8_t variant{};
                std::uint8_t state{};
                std::uint8_t reserved{};
                std::uint8_t velocity_x{};
                std::uint8_t velocity_y{};
                if (!reader.read_u8(material) || !reader.read_u8(variant) ||
                    !reader.read_u8(state) || !reader.read_u8(reserved) || reserved != 0U ||
                    material >= static_cast<std::uint8_t>(MaterialId::Count) ||
                    !reader.read_i16(cell.temperature) || !reader.read_u8(velocity_x) ||
                    !reader.read_u8(velocity_y)) {
                    return std::nullopt;
                }
                cell.material = static_cast<MaterialId>(material);
                cell.variant = variant;
                cell.state = state;
                cell.velocity_x = std::bit_cast<std::int8_t>(velocity_x);
                cell.velocity_y = std::bit_cast<std::int8_t>(velocity_y);
                cell.updated_epoch = 0;
            }
            if (!result.load_chunk_cells(static_cast<std::int32_t>(column),
                                         static_cast<std::int32_t>(row), cells)) {
                return std::nullopt;
            }
        }
    }
    result.restore_tick(tick);
    return result;
}

} // namespace meat2d::persistence
