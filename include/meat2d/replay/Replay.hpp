#pragma once

#include "meat2d/core/Types.hpp"
#include "meat2d/sim/Material.hpp"
#include "meat2d/sim/World.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace meat2d::replay {

// A single external mutation applied to the world between ticks: painting
// material with the brush. Cellular reactions and movement are already a
// pure function of world state, seed, and tick, so replaying only paint
// events reproduces an identical *World*-level run. `tick` is the world's
// current_tick() at the moment the paint is applied, i.e. it is applied
// immediately before the step() call that would advance the world past that
// tick.
//
// Scope: this module replays meat2d::World only. It does not cover
// meat2d::ai::LivingSimulation's agents or life::OrganismField — those carry
// their own per-entity state (hunger, genome, position) and can mutate world
// cells (dig/place commands) from decisions this log does not capture. A
// session that spawns agents or uses the organism brush needs its own
// recording layer built the same way; do not assume a ReplayLog reproduces
// one of those sessions.
struct PaintEvent {
    Tick tick{};
    Vec2i position{};
    std::int32_t radius{};
    MaterialId material{MaterialId::Empty};
};

// A recorded state_hash() taken immediately after the step() call that
// brought the world to `tick`. Comparing every checkpoint during playback
// pinpoints a determinism regression to an exact tick instead of only "the
// final hash differs".
struct Checkpoint {
    Tick tick{};
    std::uint64_t state_hash{};
};

// Accumulates paint events and checkpoints during a live run (or a test) and
// serializes them alongside the originating WorldConfig so the run can be
// reproduced later with replay::play. Not itself a World — a recorder is
// meant to run alongside whatever World the caller is already stepping.
class ReplayLog {
  public:
    explicit ReplayLog(WorldConfig config = {});

    void record_paint(Tick tick, Vec2i position, std::int32_t radius, MaterialId material);
    void record_checkpoint(Tick tick, std::uint64_t state_hash);

    [[nodiscard]] const WorldConfig& config() const noexcept;
    [[nodiscard]] std::span<const PaintEvent> paint_events() const noexcept;
    [[nodiscard]] std::span<const Checkpoint> checkpoints() const noexcept;

    [[nodiscard]] std::vector<std::uint8_t> encode() const;
    // Replaces *this with the decoded log on success and leaves it
    // unchanged on failure (truncated/corrupt bytes, unknown version).
    [[nodiscard]] bool decode(std::span<const std::uint8_t> bytes);

  private:
    WorldConfig config_;
    std::vector<PaintEvent> paint_events_;
    std::vector<Checkpoint> checkpoints_;
};

enum class ReplayOutcome : std::uint8_t { Matched, Diverged };

struct ReplayResult {
    ReplayOutcome outcome{ReplayOutcome::Matched};
    Tick ticks_played{};
    // Only meaningful when outcome == Diverged.
    Tick divergent_tick{};
    std::uint64_t expected_hash{};
    std::uint64_t actual_hash{};
};

// Deterministically re-simulates a ReplayLog from tick zero for
// `total_ticks` steps, applying paint events at their recorded tick and
// comparing every recorded checkpoint against the freshly computed hash.
// Stops at the first mismatch; the log's own World construction (same seed)
// makes this reproduce an identical run given identical engine behavior, so
// a Diverged result means either the inputs were mis-recorded or the
// simulation's determinism contract has regressed.
[[nodiscard]] ReplayResult play(const ReplayLog& log, Tick total_ticks);

} // namespace meat2d::replay
