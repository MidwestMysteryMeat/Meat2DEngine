#include "meat2d/sim/ChunkStore.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace meat2d {
namespace {

constexpr std::array<std::uint8_t, 4> file_magic{{'M', '2', 'C', 'K'}};
constexpr std::uint8_t file_version = 1;

} // namespace

ChunkStore::ChunkStore(std::filesystem::path directory) : directory_(std::move(directory)) {}

std::filesystem::path ChunkStore::chunk_path(std::int32_t column, std::int32_t row) const {
    return directory_ /
           ("chunk_" + std::to_string(column) + "_" + std::to_string(row) + ".m2dchunk");
}

bool ChunkStore::save_chunk(const World& world, std::int32_t column, std::int32_t row) const {
    const auto cells = world.chunk_cells(column, row);
    if (cells.empty()) {
        return false;
    }

    std::error_code error;
    std::filesystem::create_directories(directory_, error);
    if (error) {
        return false;
    }

    std::ofstream file(chunk_path(column, row), std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file.write(
        reinterpret_cast<const char*>(file_magic.data()),
        static_cast<std::streamsize>(file_magic.size()));
    file.write(reinterpret_cast<const char*>(&file_version), 1);
    file.write(
        reinterpret_cast<const char*>(cells.data()), static_cast<std::streamsize>(cells.size_bytes()));
    return file.good();
}

std::size_t ChunkStore::save_all(const World& world) const {
    std::size_t saved = 0;
    for (std::int32_t row = 0; row < world.chunk_rows(); ++row) {
        for (std::int32_t column = 0; column < world.chunk_columns(); ++column) {
            if (save_chunk(world, column, row)) {
                ++saved;
            }
        }
    }
    return saved;
}

bool ChunkStore::load_chunk(World& world, std::int32_t column, std::int32_t row) const {
    std::ifstream file(chunk_path(column, row), std::ios::binary);
    if (!file) {
        return false;
    }

    std::array<std::uint8_t, file_magic.size()> magic{};
    file.read(reinterpret_cast<char*>(magic.data()), static_cast<std::streamsize>(magic.size()));
    if (!file || magic != file_magic) {
        return false;
    }

    std::uint8_t version = 0;
    file.read(reinterpret_cast<char*>(&version), 1);
    if (!file || version != file_version) {
        return false;
    }

    std::vector<Cell> cells(cells_per_chunk);
    file.read(
        reinterpret_cast<char*>(cells.data()),
        static_cast<std::streamsize>(cells.size() * sizeof(Cell)));
    if (!file) {
        return false;
    }

    return world.load_chunk_cells(column, row, cells);
}

std::size_t ChunkStore::load_all(World& world) const {
    std::size_t loaded = 0;
    for (std::int32_t row = 0; row < world.chunk_rows(); ++row) {
        for (std::int32_t column = 0; column < world.chunk_columns(); ++column) {
            if (load_chunk(world, column, row)) {
                ++loaded;
            }
        }
    }
    return loaded;
}

bool ChunkStore::has_chunk(std::int32_t column, std::int32_t row) const {
    std::error_code error;
    const auto exists = std::filesystem::exists(chunk_path(column, row), error);
    return exists && !error;
}

} // namespace meat2d
