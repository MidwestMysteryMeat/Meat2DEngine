#include "meat2d/sim/ChunkStore.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace meat2d {
namespace {

constexpr std::array<std::uint8_t, 4> file_magic{{'M', '2', 'C', 'K'}};
constexpr std::uint8_t file_version = 1;
constexpr std::array<std::uint8_t, 4> manifest_magic{{'M', '2', 'G', 'N'}};
constexpr std::uint8_t manifest_version = 1;

std::filesystem::path chunk_path_in(const std::filesystem::path& directory,
                                     std::int32_t column, std::int32_t row) {
    return directory /
           ("chunk_" + std::to_string(column) + "_" + std::to_string(row) + ".m2dchunk");
}

std::filesystem::path manifest_path(const std::filesystem::path& directory) {
    return directory / "current.m2dmanifest";
}

std::filesystem::path generation_directory(const std::filesystem::path& directory,
                                            std::uint64_t generation) {
    return directory / ("generation_" + std::to_string(generation));
}

std::filesystem::path temporary_path(const std::filesystem::path& target) {
    auto path = target;
    path += ".tmp";
    return path;
}

std::filesystem::path backup_path(const std::filesystem::path& target) {
    auto path = target;
    path += ".bak";
    return path;
}

void recover_chunk_file(const std::filesystem::path& target) {
    const auto temporary = temporary_path(target);
    const auto backup = backup_path(target);
    std::error_code error;
    const bool target_exists = std::filesystem::exists(target, error) && !error;
    error.clear();
    const bool backup_exists = std::filesystem::exists(backup, error) && !error;
    if (!target_exists && backup_exists) {
        std::filesystem::rename(backup, target, error);
    } else if (target_exists && backup_exists) {
        std::filesystem::remove(backup, error);
    }
    error.clear();
    std::filesystem::remove(temporary, error);
}

void recover_manifest(const std::filesystem::path& directory) {
    const auto target = manifest_path(directory);
    const auto temporary = temporary_path(target);
    const auto backup = backup_path(target);
    std::error_code error;
    const bool target_exists = std::filesystem::exists(target, error) && !error;
    error.clear();
    const bool backup_exists = std::filesystem::exists(backup, error) && !error;
    if (!target_exists && backup_exists) {
        std::filesystem::rename(backup, target, error);
    } else if (target_exists && backup_exists) {
        std::filesystem::remove(backup, error);
    }
    error.clear();
    std::filesystem::remove(temporary, error);
}

std::optional<std::uint64_t> active_generation(const std::filesystem::path& directory) {
    recover_manifest(directory);
    std::ifstream file(manifest_path(directory), std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    std::array<std::uint8_t, manifest_magic.size()> magic{};
    file.read(reinterpret_cast<char*>(magic.data()), static_cast<std::streamsize>(magic.size()));
    if (!file || magic != manifest_magic) {
        return std::nullopt;
    }
    std::uint8_t version = 0;
    std::uint64_t generation = 0;
    file.read(reinterpret_cast<char*>(&version), 1);
    file.read(reinterpret_cast<char*>(&generation), static_cast<std::streamsize>(sizeof(generation)));
    if (!file || version != manifest_version || generation == 0U) {
        return std::nullopt;
    }
    return generation;
}

bool write_active_generation(const std::filesystem::path& directory,
                             std::uint64_t generation) {
    const auto target = manifest_path(directory);
    const auto temporary = temporary_path(target);
    const auto backup = backup_path(target);
    recover_manifest(directory);

    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(manifest_magic.data()),
               static_cast<std::streamsize>(manifest_magic.size()));
    file.write(reinterpret_cast<const char*>(&manifest_version), 1);
    file.write(reinterpret_cast<const char*>(&generation),
               static_cast<std::streamsize>(sizeof(generation)));
    file.flush();
    const bool write_succeeded = file.good();
    file.close();
    if (!write_succeeded) {
        std::error_code cleanup_error;
        std::filesystem::remove(temporary, cleanup_error);
        return false;
    }

    std::error_code error;
    if (std::filesystem::exists(target, error)) {
        std::filesystem::rename(target, backup, error);
        if (error) {
            std::filesystem::remove(temporary, error);
            return false;
        }
    }
    error.clear();
    std::filesystem::rename(temporary, target, error);
    if (error) {
        std::error_code restore_error;
        if (std::filesystem::exists(backup, restore_error)) {
            std::filesystem::rename(backup, target, restore_error);
        }
        std::filesystem::remove(temporary, restore_error);
        return false;
    }
    std::filesystem::remove(backup, error);
    return true;
}

bool save_chunk_to_directory(const World& world, std::int32_t column, std::int32_t row,
                             const std::filesystem::path& directory) {
    const auto cells = world.chunk_cells(column, row);
    if (cells.empty()) {
        return false;
    }

    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return false;
    }

    const auto target = chunk_path_in(directory, column, row);
    const auto temporary = temporary_path(target);
    const auto backup = backup_path(target);
    recover_chunk_file(target);

    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(file_magic.data()),
               static_cast<std::streamsize>(file_magic.size()));
    file.write(reinterpret_cast<const char*>(&file_version), 1);
    file.write(reinterpret_cast<const char*>(cells.data()),
               static_cast<std::streamsize>(cells.size_bytes()));
    file.flush();
    const bool write_succeeded = file.good();
    file.close();
    if (!write_succeeded) {
        std::filesystem::remove(temporary, error);
        return false;
    }

    error.clear();
    if (std::filesystem::exists(target, error)) {
        std::filesystem::rename(target, backup, error);
        if (error) {
            std::filesystem::remove(temporary, error);
            return false;
        }
    }
    error.clear();
    std::filesystem::rename(temporary, target, error);
    if (error) {
        std::error_code restore_error;
        if (std::filesystem::exists(backup, restore_error)) {
            std::filesystem::rename(backup, target, restore_error);
        }
        std::filesystem::remove(temporary, restore_error);
        return false;
    }
    std::filesystem::remove(backup, error);
    return true;
}

} // namespace

ChunkStore::ChunkStore(std::filesystem::path directory) : directory_(std::move(directory)) {}

std::filesystem::path ChunkStore::chunk_path(std::int32_t column, std::int32_t row) const {
    return chunk_path_in(directory_, column, row);
}

bool ChunkStore::save_chunk(const World& world, std::int32_t column, std::int32_t row) const {
    std::error_code error;
    std::filesystem::create_directories(directory_, error);
    if (error) {
        return false;
    }

    const auto generation = active_generation(directory_);
    const auto target_directory = generation.has_value()
                                      ? generation_directory(directory_, *generation)
                                      : directory_;
    return save_chunk_to_directory(world, column, row, target_directory);
}

std::size_t ChunkStore::save_all(const World& world) const {
    std::error_code error;
    std::filesystem::create_directories(directory_, error);
    if (error) {
        return 0;
    }

    const auto previous_generation = active_generation(directory_);
    const auto generation = previous_generation.value_or(0U) + 1U;
    const auto staging = directory_ / (".generation_" + std::to_string(generation) + ".tmp");
    const auto committed = generation_directory(directory_, generation);
    std::filesystem::remove_all(staging, error);
    if (error) {
        return 0;
    }
    std::filesystem::remove_all(committed, error);
    if (error || !std::filesystem::create_directories(staging, error) || error) {
        return 0;
    }

    std::size_t saved = 0;
    for (std::int32_t row = 0; row < world.chunk_rows(); ++row) {
        for (std::int32_t column = 0; column < world.chunk_columns(); ++column) {
            if (save_chunk_to_directory(world, column, row, staging)) {
                ++saved;
            }
        }
    }

    const auto expected = static_cast<std::size_t>(world.chunk_columns()) *
                          static_cast<std::size_t>(world.chunk_rows());
    if (saved != expected) {
        std::filesystem::remove_all(staging, error);
        return 0;
    }
    std::filesystem::rename(staging, committed, error);
    if (error || !write_active_generation(directory_, generation)) {
        std::filesystem::remove_all(staging, error);
        return 0;
    }
    return saved;
}

bool ChunkStore::load_chunk(World& world, std::int32_t column, std::int32_t row) const {
    const auto generation = active_generation(directory_);
    const auto target = generation.has_value()
                            ? chunk_path_in(generation_directory(directory_, *generation), column,
                                            row)
                            : chunk_path(column, row);
    recover_chunk_file(target);
    std::ifstream file(target, std::ios::binary);
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
    const auto generation = active_generation(directory_);
    const auto target = generation.has_value()
                            ? chunk_path_in(generation_directory(directory_, *generation), column,
                                            row)
                            : chunk_path(column, row);
    std::error_code error;
    const auto exists = std::filesystem::exists(target, error);
    return exists && !error;
}

} // namespace meat2d
