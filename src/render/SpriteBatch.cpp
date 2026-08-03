#include "meat2d/render/SpriteBatch.hpp"

#include <algorithm>
#include <limits>

namespace meat2d::render {
namespace {

std::int32_t saturating_dimension(std::int64_t value) noexcept {
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(
        value, 1, std::numeric_limits<std::int32_t>::max()));
}

std::int32_t scaled_dimension(std::int32_t value, std::int32_t scale) noexcept {
    const auto magnitude = std::max<std::int64_t>(1, scale < 0 ? -static_cast<std::int64_t>(scale)
                                                                  : scale);
    return saturating_dimension(static_cast<std::int64_t>(std::max(value, 1)) * magnitude);
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

SpriteBatch::SpriteBatch(std::size_t maximum_commands)
    : maximum_commands_(std::clamp(maximum_commands, std::size_t{1}, maximum_sprite_commands)) {
    commands_.reserve(maximum_commands_);
}

bool SpriteBatch::build(const scene::Scene& scene, const Camera2D& camera, bool visible_only) {
    std::vector<SpriteDrawCommand> next;
    next.reserve(std::min(maximum_commands_, scene.entities().size()));
    const auto visible = camera.visible_rect();
    for (const auto& entity : scene.entities()) {
        if (!entity.sprite || (visible_only && !entity.sprite->visible)) {
            continue;
        }
        const auto world = scene.world_position(entity.id);
        const auto source_width = entity.sprite->source.width > 0 ? entity.sprite->source.width : 1;
        const auto source_height =
            entity.sprite->source.height > 0 ? entity.sprite->source.height : 1;
        const auto scale_x = entity.transform ? entity.transform->scale.x : 1;
        const auto scale_y = entity.transform ? entity.transform->scale.y : 1;
        const RectI world_bounds{
            world.x,
            world.y,
            scaled_dimension(source_width, scale_x),
            scaled_dimension(source_height, scale_y),
        };
        if (!overlaps(world_bounds, visible)) {
            continue;
        }
        if (next.size() == maximum_commands_) {
            return false;
        }
        const auto screen = camera.world_to_screen(world);
        const auto zoom = camera.zoom_percent();
        next.push_back({
            .entity = entity.id,
            .asset_id = entity.sprite->asset_id,
            .source = entity.sprite->source,
            .destination = {
                screen.x,
                screen.y,
                saturating_dimension(static_cast<std::int64_t>(world_bounds.width) * zoom / 100),
                saturating_dimension(static_cast<std::int64_t>(world_bounds.height) * zoom / 100),
            },
            .layer = entity.sprite->layer,
            .flip_x = scale_x < 0,
            .flip_y = scale_y < 0,
        });
    }
    std::stable_sort(next.begin(), next.end(), [](const auto& left, const auto& right) {
        if (left.layer != right.layer) {
            return left.layer < right.layer;
        }
        return left.entity < right.entity;
    });
    commands_ = std::move(next);
    return true;
}

void SpriteBatch::clear() noexcept {
    commands_.clear();
}

std::span<const SpriteDrawCommand> SpriteBatch::commands() const noexcept {
    return std::span<const SpriteDrawCommand>(commands_);
}

std::size_t SpriteBatch::maximum_commands() const noexcept {
    return maximum_commands_;
}

} // namespace meat2d::render
