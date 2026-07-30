#include "meat2d/net/ChunkCodec.hpp"

#include "meat2d/net/PacketCodec.hpp"
#include "meat2d/sim/Chunk.hpp"
#include "meat2d/sim/World.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace meat2d::net {
namespace {

bool network_equal(const Cell& first, const Cell& second) noexcept {
    return first.material == second.material && first.variant == second.variant &&
           first.state == second.state &&
           first.temperature == second.temperature &&
           first.velocity_x == second.velocity_x &&
           first.velocity_y == second.velocity_y;
}

bool write_cell(ByteWriter& writer, const Cell& cell) {
    return writer.write_u8(static_cast<std::uint8_t>(cell.material)) &&
           writer.write_u8(cell.variant) && writer.write_u8(cell.state) &&
           writer.write_i16(cell.temperature) &&
           writer.write_u8(static_cast<std::uint8_t>(cell.velocity_x)) &&
           writer.write_u8(static_cast<std::uint8_t>(cell.velocity_y));
}

bool read_cell(ByteReader& reader, Cell& cell) {
    std::uint8_t material = 0;
    std::uint8_t velocity_x = 0;
    std::uint8_t velocity_y = 0;
    if (!reader.read_u8(material) || !reader.read_u8(cell.variant) ||
        !reader.read_u8(cell.state) || !reader.read_i16(cell.temperature) ||
        !reader.read_u8(velocity_x) || !reader.read_u8(velocity_y) ||
        !is_valid(static_cast<MaterialId>(material))) {
        return false;
    }
    cell.material = static_cast<MaterialId>(material);
    cell.velocity_x = static_cast<std::int8_t>(velocity_x);
    cell.velocity_y = static_cast<std::int8_t>(velocity_y);
    cell.updated_epoch = 0;
    return true;
}

} // namespace

std::optional<std::vector<std::uint8_t>> encode_chunk_delta(
    const World& world,
    std::size_t chunk_index) {
    const auto chunks = world.chunks();
    if (chunk_index >= chunks.size()) {
        return std::nullopt;
    }
    const auto chunk_x =
        static_cast<std::uint16_t>(chunk_index % static_cast<std::size_t>(world.chunk_columns()));
    const auto chunk_y =
        static_cast<std::uint16_t>(chunk_index / static_cast<std::size_t>(world.chunk_columns()));
    const auto& chunk = chunks[chunk_index];

    ByteWriter writer(maximum_chunk_delta_bytes);
    if (!writer.write_u8(chunk_codec_version) || !writer.write_u16(chunk_x) ||
        !writer.write_u16(chunk_y) || !writer.write_u64(chunk.revision) ||
        !writer.write_u16(static_cast<std::uint16_t>(cells_per_chunk))) {
        return std::nullopt;
    }

    std::size_t start = 0;
    while (start < chunk.cells.size()) {
        std::size_t end = start + 1;
        while (end < chunk.cells.size() &&
               end - start < std::numeric_limits<std::uint16_t>::max() &&
               network_equal(chunk.cells[start], chunk.cells[end])) {
            ++end;
        }
        const auto run_length = static_cast<std::uint16_t>(end - start);
        if (!writer.write_u16(run_length) || !write_cell(writer, chunk.cells[start])) {
            return std::nullopt;
        }
        start = end;
    }
    return writer.take();
}

std::optional<ChunkDeltaInfo> apply_chunk_delta(
    World& world,
    std::span<const std::uint8_t> payload) {
    ByteReader reader(payload);
    std::uint8_t version = 0;
    std::uint16_t chunk_x = 0;
    std::uint16_t chunk_y = 0;
    std::uint64_t revision = 0;
    std::uint16_t cell_count = 0;
    if (!reader.read_u8(version) || !reader.read_u16(chunk_x) ||
        !reader.read_u16(chunk_y) || !reader.read_u64(revision) ||
        !reader.read_u16(cell_count) || version != chunk_codec_version ||
        cell_count != cells_per_chunk ||
        chunk_x >= static_cast<std::uint16_t>(world.chunk_columns()) ||
        chunk_y >= static_cast<std::uint16_t>(world.chunk_rows())) {
        return std::nullopt;
    }

    std::array<Cell, cells_per_chunk> decoded{};
    std::size_t output = 0;
    while (output < decoded.size()) {
        std::uint16_t run_length = 0;
        Cell cell{};
        if (!reader.read_u16(run_length) || run_length == 0U ||
            !read_cell(reader, cell) ||
            run_length > decoded.size() - output) {
            return std::nullopt;
        }
        std::fill_n(decoded.begin() + static_cast<std::ptrdiff_t>(output), run_length, cell);
        output += run_length;
    }
    if (!reader.empty()) {
        return std::nullopt;
    }

    ChunkDeltaInfo info{
        .chunk_x = chunk_x,
        .chunk_y = chunk_y,
        .revision = revision,
    };
    for (std::int32_t local_y = 0; local_y < chunk_size; ++local_y) {
        for (std::int32_t local_x = 0; local_x < chunk_size; ++local_x) {
            const Vec2i position{
                static_cast<std::int32_t>(chunk_x) * chunk_size + local_x,
                static_cast<std::int32_t>(chunk_y) * chunk_size + local_y,
            };
            if (!world.in_bounds(position)) {
                continue;
            }
            const auto cell_index = Chunk::index(local_x, local_y);
            if (world.set_cell(position, decoded[cell_index])) {
                ++info.changed_cells;
            }
        }
    }
    return info;
}

std::vector<std::size_t> interested_chunks(
    const World& world,
    Vec2i focus,
    std::uint8_t radius_in_chunks) {
    const auto clamped_x = std::clamp(focus.x, 0, world.width() - 1);
    const auto clamped_y = std::clamp(focus.y, 0, world.height() - 1);
    const auto center_x = clamped_x / chunk_size;
    const auto center_y = clamped_y / chunk_size;
    const auto radius = static_cast<std::int32_t>(radius_in_chunks);
    std::vector<std::size_t> result;
    for (std::int32_t y = center_y - radius; y <= center_y + radius; ++y) {
        for (std::int32_t x = center_x - radius; x <= center_x + radius; ++x) {
            if (x < 0 || y < 0 || x >= world.chunk_columns() ||
                y >= world.chunk_rows()) {
                continue;
            }
            result.push_back(static_cast<std::size_t>(y * world.chunk_columns() + x));
        }
    }
    return result;
}

} // namespace meat2d::net
