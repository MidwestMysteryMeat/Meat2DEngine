#pragma once

#include "meat2d/core/Types.hpp"
#include "meat2d/sim/Chunk.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace meat2d {

struct WorldConfig {
    std::int32_t width{320};
    std::int32_t height{180};
    std::uint64_t seed{0x4D4541543244ULL};
    std::uint16_t sleep_after_ticks{30};
};

struct TickStats {
    Tick tick{};
    std::uint64_t moved_cells{};
    std::uint64_t reacted_cells{};
    std::uint64_t heat_transfers{};
    std::uint32_t changed_chunks{};
    std::uint32_t active_chunks{};
    std::uint32_t sleeping_chunks{};
};

class World {
  public:
    explicit World(WorldConfig config = {});

    [[nodiscard]] std::int32_t width() const noexcept;
    [[nodiscard]] std::int32_t height() const noexcept;
    [[nodiscard]] Tick current_tick() const noexcept;
    [[nodiscard]] std::uint64_t seed() const noexcept;
    [[nodiscard]] bool in_bounds(Vec2i position) const noexcept;

    [[nodiscard]] const Cell& cell(Vec2i position) const;
    [[nodiscard]] MaterialId material(Vec2i position) const;
    bool set_cell(Vec2i position, Cell value);
    bool set_material(Vec2i position, MaterialId material);
    std::size_t paint_disc(Vec2i center, std::int32_t radius, MaterialId material);

    TickStats step();
    void wake_all() noexcept;
    void clear_dirty() noexcept;

    [[nodiscard]] std::uint64_t state_hash() const noexcept;
    void rasterize_rgba(std::span<std::uint8_t> destination) const;
    void rasterize_rgba_region(RectI region, std::span<std::uint8_t> destination) const;

    [[nodiscard]] std::int32_t chunk_columns() const noexcept;
    [[nodiscard]] std::int32_t chunk_rows() const noexcept;
    [[nodiscard]] std::span<const Chunk> chunks() const noexcept;
    [[nodiscard]] RectI chunk_dirty_rect(std::int32_t column, std::int32_t row) const noexcept;

  private:
    [[nodiscard]] std::size_t chunk_index(Vec2i position) const noexcept;
    [[nodiscard]] std::size_t local_index(Vec2i position) const noexcept;
    [[nodiscard]] Cell& cell_unchecked(Vec2i position) noexcept;
    [[nodiscard]] const Cell& cell_unchecked(Vec2i position) const noexcept;

    void update_cell(Vec2i position, std::uint8_t epoch, TickStats& stats);
    bool try_move(Vec2i from, Vec2i to, std::uint8_t epoch, TickStats& stats);
    bool transform_cell(
        Vec2i position,
        MaterialId material,
        std::uint8_t epoch,
        TickStats& stats,
        bool preserve_temperature);
    bool apply_phase_change(Vec2i position, std::uint8_t epoch, TickStats& stats);
    void exchange_heat(Vec2i position, TickStats& stats);
    void update_fire(Vec2i position, std::uint8_t epoch, TickStats& stats);
    void update_acid(Vec2i position, std::uint8_t epoch, TickStats& stats);
    void update_lava(Vec2i position, std::uint8_t epoch, TickStats& stats);
    void update_plant(Vec2i position, std::uint8_t epoch, TickStats& stats);
    void update_electricity(Vec2i position, std::uint8_t epoch, TickStats& stats);
    void update_charged_metal(Vec2i position, std::uint8_t epoch);
    void explode(Vec2i center, std::int32_t radius, std::uint8_t epoch, TickStats& stats);
    [[nodiscard]] bool has_neighbor(Vec2i position, MaterialId material) const noexcept;
    [[nodiscard]] std::uint8_t initial_state(MaterialId material) const noexcept;
    void mark_changed(Vec2i position) noexcept;
    void wake_neighborhood(Vec2i position) noexcept;
    void reset_update_epochs() noexcept;
    [[nodiscard]] std::uint64_t noise(Vec2i position, std::uint64_t salt) const noexcept;

    WorldConfig config_;
    std::int32_t chunk_columns_{};
    std::int32_t chunk_rows_{};
    std::vector<Chunk> chunks_;
    Tick tick_{};
};

} // namespace meat2d
