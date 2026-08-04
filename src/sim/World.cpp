#include "meat2d/sim/World.hpp"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace meat2d {
namespace {

} // namespace

World::World(WorldConfig config) : config_(config) {
    if (config_.width <= 0 || config_.height <= 0) {
        throw std::invalid_argument("world dimensions must be positive");
    }
    if (config_.sleep_after_ticks == 0) {
        throw std::invalid_argument("sleep_after_ticks must be positive");
    }

    chunk_columns_ = (config_.width + chunk_size - 1) / chunk_size;
    chunk_rows_ = (config_.height + chunk_size - 1) / chunk_size;
    chunks_.resize(static_cast<std::size_t>(chunk_columns_ * chunk_rows_));
}

TickStats World::step() {
    ++tick_;
    const auto epoch = static_cast<std::uint8_t>(((tick_ - 1U) % 255U) + 1U);
    if (epoch == 1U && tick_ > 1U) {
        reset_update_epochs();
    }

    std::vector<std::uint8_t> active_at_start(chunks_.size(), 0);
    for (std::size_t index = 0; index < chunks_.size(); ++index) {
        active_at_start[index] = chunks_[index].active ? 1U : 0U;
    }

    TickStats stats{};
    stats.tick = tick_;

    for (std::int32_t y = config_.height - 1; y >= 0; --y) {
        const bool left_to_right = (noise({0, y}, tick_) & 1U) == 0U;
        for (std::int32_t offset = 0; offset < config_.width; ++offset) {
            const std::int32_t x = left_to_right ? offset : config_.width - 1 - offset;
            const Vec2i position{x, y};
            if (active_at_start[chunk_index(position)] != 0U) {
                update_cell(position, epoch, stats);
            }
        }
    }

    finalize_tick(stats, active_at_start);
    return stats;
}

void World::finalize_tick(TickStats& stats, std::span<const std::uint8_t> active_at_start) noexcept {
    for (std::size_t index = 0; index < chunks_.size(); ++index) {
        auto& chunk = chunks_[index];
        if (chunk.changed) {
            ++chunk.revision;
            chunk.quiet_ticks = 0;
            chunk.active = true;
            chunk.changed = false;
            ++stats.changed_chunks;
        } else if (active_at_start[index] != 0U) {
            if (chunk.quiet_ticks < std::numeric_limits<std::uint16_t>::max()) {
                ++chunk.quiet_ticks;
            }
            if (chunk.quiet_ticks >= config_.sleep_after_ticks) {
                chunk.active = false;
            }
        }

        if (chunk.active) {
            ++stats.active_chunks;
        } else {
            ++stats.sleeping_chunks;
        }
    }
}

void World::process_chunk_cells(
    std::int32_t column,
    std::int32_t row,
    std::uint8_t epoch,
    TickStats& stats) {
    const auto x_begin = column * chunk_size;
    const auto x_end = std::min(x_begin + chunk_size, config_.width);
    const auto y_begin = row * chunk_size;
    const auto y_end = std::min(y_begin + chunk_size, config_.height);

    for (std::int32_t y = y_end - 1; y >= y_begin; --y) {
        const bool left_to_right = (noise({0, y}, tick_) & 1U) == 0U;
        for (std::int32_t offset = 0; offset < x_end - x_begin; ++offset) {
            const std::int32_t x =
                left_to_right ? x_begin + offset : x_end - 1 - offset;
            update_cell({x, y}, epoch, stats);
        }
    }
}

namespace {
thread_local std::vector<Vec2i>* parallel_touch_log = nullptr;

// A fixed-size pool of worker threads reused across step_parallel calls, so
// only the first call pays OS thread-creation cost. run() is a barrier: it
// blocks the calling thread until every worker in [0, active) has executed
// task(worker_index) exactly once for the current generation. Workers with
// index >= active stay parked without touching completed_count_, so the
// barrier target is exactly `active`, independent of the pool's total size.
class ChunkWorkerPool {
  public:
    explicit ChunkWorkerPool(std::size_t worker_count) {
        workers_.reserve(worker_count);
        for (std::size_t index = 0; index < worker_count; ++index) {
            workers_.emplace_back([this, index]() { worker_loop(index); });
        }
    }

    ~ChunkWorkerPool() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        condition_.notify_all();
        for (auto& worker : workers_) {
            worker.join();
        }
    }

    ChunkWorkerPool(const ChunkWorkerPool&) = delete;
    ChunkWorkerPool& operator=(const ChunkWorkerPool&) = delete;

    [[nodiscard]] std::size_t worker_count() const noexcept {
        return workers_.size();
    }

    void run(std::size_t active, const std::function<void(std::size_t)>& task) {
        if (active == 0) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            task_ = &task;
            active_count_ = active;
            completed_count_ = 0;
            ++generation_;
        }
        condition_.notify_all();

        std::unique_lock<std::mutex> lock(mutex_);
        done_condition_.wait(lock, [this]() { return completed_count_ == active_count_; });
        task_ = nullptr;
    }

  private:
    void worker_loop(std::size_t index) {
        std::uint64_t seen_generation = 0;
        while (true) {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this, seen_generation]() {
                return stopping_ || generation_ != seen_generation;
            });
            if (stopping_) {
                return;
            }
            seen_generation = generation_;
            const auto active = active_count_;
            if (index >= active) {
                continue;
            }
            const auto* task = task_;
            lock.unlock();

            (*task)(index);

            lock.lock();
            ++completed_count_;
            if (completed_count_ == active_count_) {
                done_condition_.notify_one();
            }
        }
    }

    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::condition_variable done_condition_;
    const std::function<void(std::size_t)>* task_{};
    std::size_t active_count_{};
    std::size_t completed_count_{};
    std::uint64_t generation_{};
    bool stopping_{};
};

// Always sized at hardware_concurrency regardless of any single call's
// requested worker count — run()'s `active` parameter is what varies per
// call (a worker index >= active just stays parked, see worker_loop). If
// this were sized to the first caller's request, an earlier small request
// would permanently cap every later, larger one on the same thread.
ChunkWorkerPool& parallel_worker_pool() {
    static thread_local ChunkWorkerPool pool(std::max<std::size_t>(
        1, std::thread::hardware_concurrency()));
    return pool;
}
} // namespace

TickStats World::step_parallel(std::size_t worker_count) {
    ++tick_;
    const auto epoch = static_cast<std::uint8_t>(((tick_ - 1U) % 255U) + 1U);
    if (epoch == 1U && tick_ > 1U) {
        reset_update_epochs();
    }

    std::vector<std::uint8_t> active_at_start(chunks_.size(), 0);
    for (std::size_t index = 0; index < chunks_.size(); ++index) {
        active_at_start[index] = chunks_[index].active ? 1U : 0U;
    }

    TickStats stats{};
    stats.tick = tick_;

    auto& pool = parallel_worker_pool();
    if (worker_count == 0) {
        worker_count = pool.worker_count();
    }

    // Phase (column % 2, row % 2): any two chunks sharing a phase are at
    // least two chunks apart on some axis, so they are never adjacent, not
    // even diagonally. See step_parallel's declaration for why that spacing
    // is sufficient given every reaction's maximum write reach.
    for (std::int32_t phase = 0; phase < 4; ++phase) {
        const auto phase_column_parity = phase & 1;
        const auto phase_row_parity = (phase >> 1) & 1;

        std::vector<std::pair<std::int32_t, std::int32_t>> phase_chunks;
        for (std::int32_t row = 0; row < chunk_rows_; ++row) {
            if ((row & 1) != phase_row_parity) {
                continue;
            }
            for (std::int32_t column = 0; column < chunk_columns_; ++column) {
                if ((column & 1) != phase_column_parity) {
                    continue;
                }
                if (active_at_start[static_cast<std::size_t>(row * chunk_columns_ + column)] !=
                    0U) {
                    phase_chunks.emplace_back(column, row);
                }
            }
        }
        if (phase_chunks.empty()) {
            continue;
        }

        const auto workers = std::min({worker_count, phase_chunks.size(), pool.worker_count()});
        std::vector<TickStats> worker_stats(workers);
        std::vector<std::vector<Vec2i>> worker_touches(workers);

        const std::function<void(std::size_t)> task = [this, workers, epoch, &phase_chunks,
                                                        &worker_stats, &worker_touches](
                                                            std::size_t worker) {
            parallel_touch_log = &worker_touches[worker];
            for (std::size_t index = worker; index < phase_chunks.size(); index += workers) {
                const auto [column, row] = phase_chunks[index];
                process_chunk_cells(column, row, epoch, worker_stats[worker]);
            }
            parallel_touch_log = nullptr;
        };
        pool.run(workers, task);

        // Everything below runs single-threaded: safe to call the direct-
        // write mark_changed/wake_neighborhood again for every logged
        // position now that no worker is still running.
        for (const auto& touches : worker_touches) {
            for (const auto& position : touches) {
                mark_changed(position);
            }
        }
        for (const auto& worker_stat : worker_stats) {
            stats.moved_cells += worker_stat.moved_cells;
            stats.reacted_cells += worker_stat.reacted_cells;
            stats.heat_transfers += worker_stat.heat_transfers;
        }
    }

    finalize_tick(stats, active_at_start);
    return stats;
}

void World::wake_all() noexcept {
    for (auto& chunk : chunks_) {
        chunk.active = true;
        chunk.quiet_ticks = 0;
    }
}

void World::clear_dirty() noexcept {
    for (auto& chunk : chunks_) {
        chunk.dirty.clear();
    }
}

std::size_t World::chunk_index(Vec2i position) const noexcept {
    const auto chunk_x = position.x / chunk_size;
    const auto chunk_y = position.y / chunk_size;
    return static_cast<std::size_t>(chunk_y * chunk_columns_ + chunk_x);
}

std::size_t World::local_index(Vec2i position) const noexcept {
    return Chunk::index(position.x % chunk_size, position.y % chunk_size);
}

Cell& World::cell_unchecked(Vec2i position) noexcept {
    return chunks_[chunk_index(position)].cells[local_index(position)];
}

const Cell& World::cell_unchecked(Vec2i position) const noexcept {
    return chunks_[chunk_index(position)].cells[local_index(position)];
}

void World::restore_tick(Tick tick) noexcept {
    tick_ = tick;
}

std::uint16_t World::sleep_after_ticks() const noexcept {
    return config_.sleep_after_ticks;
}
std::uint8_t World::initial_state(MaterialId material_id) const noexcept {
    switch (material_id) {
    case MaterialId::Fire:
        return 180U;
    case MaterialId::Smoke:
        return 120U;
    case MaterialId::Acid:
        return 168U;
    case MaterialId::Electricity:
        return 12U;
    default:
        return 0U;
    }
}

void World::mark_changed(Vec2i position) noexcept {
    if (parallel_touch_log != nullptr) {
        parallel_touch_log->push_back(position);
        return;
    }
    auto& chunk = chunks_[chunk_index(position)];
    chunk.changed = true;
    chunk.active = true;
    chunk.quiet_ticks = 0;
    chunk.dirty.include(position.x % chunk_size, position.y % chunk_size);
    wake_neighborhood(position);
}

void World::wake_neighborhood(Vec2i position) noexcept {
    const auto center_x = position.x / chunk_size;
    const auto center_y = position.y / chunk_size;
    for (std::int32_t chunk_y = center_y - 1; chunk_y <= center_y + 1; ++chunk_y) {
        for (std::int32_t chunk_x = center_x - 1; chunk_x <= center_x + 1; ++chunk_x) {
            if (chunk_x < 0 || chunk_y < 0 || chunk_x >= chunk_columns_ ||
                chunk_y >= chunk_rows_) {
                continue;
            }
            auto& chunk =
                chunks_[static_cast<std::size_t>(chunk_y * chunk_columns_ + chunk_x)];
            chunk.active = true;
            chunk.quiet_ticks = 0;
        }
    }
}

void World::reset_update_epochs() noexcept {
    for (auto& chunk : chunks_) {
        for (auto& value : chunk.cells) {
            value.updated_epoch = 0;
        }
    }
}

std::uint64_t World::noise(Vec2i position, std::uint64_t salt) const noexcept {
    std::uint64_t value = config_.seed ^ salt;
    value ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(position.x)) *
             0x9E3779B185EBCA87ULL;
    value ^= static_cast<std::uint64_t>(static_cast<std::uint32_t>(position.y)) *
             0xC2B2AE3D27D4EB4FULL;
    value ^= value >> 30U;
    value *= 0xBF58476D1CE4E5B9ULL;
    value ^= value >> 27U;
    value *= 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

} // namespace meat2d
