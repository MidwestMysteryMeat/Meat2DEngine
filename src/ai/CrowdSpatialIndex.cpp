#include "meat2d/ai/CrowdSpatialIndex.hpp"

#include <algorithm>
#include <limits>

namespace meat2d::ai {
namespace {

std::int32_t floor_divide(std::int64_t value, std::int32_t divisor) noexcept {
    const auto quotient = value / divisor;
    const auto remainder = value % divisor;
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(
        remainder < 0 ? quotient - 1 : quotient, std::numeric_limits<std::int32_t>::min(),
        std::numeric_limits<std::int32_t>::max()));
}

Vec2i spatial_cell(Vec2i position) noexcept {
    return {floor_divide(position.x, crowd_spatial_cell_size),
            floor_divide(position.y, crowd_spatial_cell_size)};
}

bool cell_less(Vec2i left, Vec2i right) noexcept {
    return left.y != right.y ? left.y < right.y : left.x < right.x;
}

} // namespace

CrowdSpatialIndex::CrowdSpatialIndex(std::size_t maximum_index_agents) {
    const auto bounded =
        std::clamp(maximum_index_agents, std::size_t{1}, maximum_crowd_agents);
    entries_.reserve(bounded);
    candidates_.reserve(bounded);
}

void CrowdSpatialIndex::rebuild(std::span<const CrowdAgent> agents) {
    entries_.clear();
    entries_.reserve(agents.size());
    for (std::size_t index = 0; index < agents.size(); ++index) {
        entries_.push_back({.cell = spatial_cell(agents[index].position),
                            .id = agents[index].id,
                            .agent_index = index});
    }
    std::sort(entries_.begin(), entries_.end(), [](const auto& left, const auto& right) {
        if (left.cell != right.cell) {
            return cell_less(left.cell, right.cell);
        }
        return left.id < right.id;
    });
}

std::span<const std::size_t> CrowdSpatialIndex::query(Vec2i position,
                                                      std::int32_t radius) {
    candidates_.clear();
    if (radius < 0) {
        return std::span<const std::size_t>(candidates_);
    }
    const auto min_cell = Vec2i{
        floor_divide(static_cast<std::int64_t>(position.x) - radius, crowd_spatial_cell_size),
        floor_divide(static_cast<std::int64_t>(position.y) - radius, crowd_spatial_cell_size)};
    const auto max_cell = Vec2i{
        floor_divide(static_cast<std::int64_t>(position.x) + radius, crowd_spatial_cell_size),
        floor_divide(static_cast<std::int64_t>(position.y) + radius, crowd_spatial_cell_size)};
    for (std::int64_t cell_y = min_cell.y; cell_y <= max_cell.y; ++cell_y) {
        for (std::int64_t cell_x = min_cell.x; cell_x <= max_cell.x; ++cell_x) {
            const Vec2i cell{static_cast<std::int32_t>(cell_x),
                             static_cast<std::int32_t>(cell_y)};
            const auto begin = std::lower_bound(
                entries_.begin(), entries_.end(), cell,
                [](const Entry& entry, Vec2i value) { return cell_less(entry.cell, value); });
            for (auto iterator = begin;
                 iterator != entries_.end() && iterator->cell == cell; ++iterator) {
                candidates_.push_back(iterator->agent_index);
            }
        }
    }
    return std::span<const std::size_t>(candidates_);
}

} // namespace meat2d::ai
