#include "meat2d/render/StaticMeshBatch.hpp"

#include <algorithm>
#include <limits>

namespace meat2d::render {
namespace {

std::int32_t scaled_dimension(std::int32_t value, std::int32_t scale) noexcept {
    const auto magnitude = std::max<std::int64_t>(
        1, scale < 0 ? -static_cast<std::int64_t>(scale) : scale);
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(
        static_cast<std::int64_t>(std::max(value, 1)) * magnitude, 1,
        std::numeric_limits<std::int32_t>::max()));
}

std::int32_t saturating_coordinate(std::int64_t value) noexcept {
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(
        value, std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max()));
}

std::int32_t zoomed_dimension(std::int32_t value, std::int32_t zoom) noexcept {
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(
        static_cast<std::int64_t>(value) * zoom / 100, 1,
        std::numeric_limits<std::int32_t>::max()));
}

bool overlaps(RectI left, RectI right) noexcept {
    return static_cast<std::int64_t>(left.x) <
               static_cast<std::int64_t>(right.x) + right.width &&
           static_cast<std::int64_t>(right.x) < static_cast<std::int64_t>(left.x) + left.width &&
           static_cast<std::int64_t>(left.y) <
               static_cast<std::int64_t>(right.y) + right.height &&
           static_cast<std::int64_t>(right.y) < static_cast<std::int64_t>(left.y) + left.height;
}

} // namespace

StaticMeshInstanceBatch::StaticMeshInstanceBatch(std::size_t maximum_instances)
    : maximum_instances_(std::clamp(maximum_instances, std::size_t{1},
                                    maximum_static_mesh_instances)) {
    commands_.reserve(maximum_instances_);
}

bool StaticMeshInstanceBatch::build(std::span<const StaticMeshInstance> instances,
                                    const Camera2D& camera, bool visible_only) {
    std::vector<StaticMeshDrawCommand> next;
    next.reserve(std::min(maximum_instances_, instances.size()));
    const auto visible = camera.visible_rect();
    const auto zoom = camera.zoom_percent();
    for (const auto& instance : instances) {
        if (instance.mesh == invalid_static_mesh || (visible_only && !instance.visible) ||
            instance.local_bounds.empty()) {
            continue;
        }
        const RectI world_bounds{
            saturating_coordinate(static_cast<std::int64_t>(instance.position.x) +
                                  instance.local_bounds.x),
            saturating_coordinate(static_cast<std::int64_t>(instance.position.y) +
                                  instance.local_bounds.y),
            scaled_dimension(instance.local_bounds.width, instance.scale.x),
            scaled_dimension(instance.local_bounds.height, instance.scale.y),
        };
        if (!overlaps(world_bounds, visible)) {
            continue;
        }
        if (next.size() == maximum_instances_) {
            return false;
        }
        const auto screen = camera.world_to_screen(
            {world_bounds.x, world_bounds.y});
        next.push_back({.entity = instance.entity,
                        .mesh = instance.mesh,
                        .world_bounds = world_bounds,
                        .screen_bounds = {
                            screen.x,
                            screen.y,
                            zoomed_dimension(world_bounds.width, zoom),
                            zoomed_dimension(world_bounds.height, zoom),
                        },
                        .layer = instance.layer});
    }
    std::stable_sort(next.begin(), next.end(), [](const auto& left, const auto& right) {
        if (left.layer != right.layer) {
            return left.layer < right.layer;
        }
        if (left.mesh != right.mesh) {
            return left.mesh < right.mesh;
        }
        return left.entity < right.entity;
    });
    commands_ = std::move(next);
    return true;
}

void StaticMeshInstanceBatch::clear() noexcept {
    commands_.clear();
}

std::span<const StaticMeshDrawCommand> StaticMeshInstanceBatch::commands() const noexcept {
    return std::span<const StaticMeshDrawCommand>(commands_);
}

std::size_t StaticMeshInstanceBatch::maximum_instances() const noexcept {
    return maximum_instances_;
}

} // namespace meat2d::render
