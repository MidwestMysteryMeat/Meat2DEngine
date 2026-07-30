#pragma once

#include "meat2d/sim/World.hpp"

#include <cstddef>
#include <filesystem>

namespace meat2d {

// Disk-backed chunk persistence for a fixed-size World: one file per chunk
// under a directory, so a large world's cold regions don't have to stay
// resident in memory between sessions, and a running server or editor can
// save/restore state without serializing the whole grid at once.
//
// Scope: this persists chunks within a World's existing (column, row)
// bounds — it does not extend the world's addressable area. An unbounded
// world (loading chunks beyond the initial grid) needs World's fixed
// chunk_columns_/chunk_rows_ addressing to become dynamic, which is a
// larger, separate change; ChunkStore is the on-disk paging primitive that
// change would build on.
class ChunkStore {
  public:
    explicit ChunkStore(std::filesystem::path directory);

    // Writes every chunk in the world's grid to disk. Returns the number of
    // chunks written, or 0 if the directory could not be created.
    std::size_t save_all(const World& world) const;
    bool save_chunk(const World& world, std::int32_t column, std::int32_t row) const;

    // Loads a previously saved chunk's cells into `world` at (column, row).
    // Returns false if no file exists for that chunk, the chunk is out of
    // the world's bounds, or the file doesn't match the current chunk
    // layout (see cells_per_chunk).
    bool load_chunk(World& world, std::int32_t column, std::int32_t row) const;
    // Loads every chunk file present in the directory that falls within the
    // world's grid, skipping files with no match rather than failing the
    // whole load. Returns the number of chunks loaded.
    std::size_t load_all(World& world) const;

    [[nodiscard]] bool has_chunk(std::int32_t column, std::int32_t row) const;

  private:
    [[nodiscard]] std::filesystem::path chunk_path(std::int32_t column, std::int32_t row) const;

    std::filesystem::path directory_;
};

} // namespace meat2d
