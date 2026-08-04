#pragma once

#include "meat2d/render/Camera.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace meat2d::render {

using StaticMeshId = std::uint32_t;
inline constexpr StaticMeshId invalid_static_mesh = 0U;
inline constexpr std::size_t maximum_static_mesh_instances = 65'536U;

// Renderer-neutral instance data. A loader owns mesh vertices/indices; this
// batch owns only stable IDs, transforms, bounds, and visibility metadata.
struct StaticMeshInstance {
    std::uint32_t entity{};
    StaticMeshId mesh{invalid_static_mesh};
    Vec2i position{};
    Vec2i scale{1, 1};
    RectI local_bounds{0, 0, 1, 1};
    std::int16_t layer{};
    bool visible{true};
};

struct StaticMeshDrawCommand {
    std::uint32_t entity{};
    StaticMeshId mesh{invalid_static_mesh};
    RectI world_bounds{};
    RectI screen_bounds{};
    std::int16_t layer{};

    friend bool operator==(const StaticMeshDrawCommand&, const StaticMeshDrawCommand&) = default;
};

// HISM-style deterministic instance submission for repeated static geometry.
// Backends can consume consecutive mesh IDs as one instanced draw while the
// engine remains independent of SDL/OpenGL/Vulkan/Direct3D details.
class StaticMeshInstanceBatch {
  public:
    explicit StaticMeshInstanceBatch(
        std::size_t maximum_instances = maximum_static_mesh_instances);

    bool build(std::span<const StaticMeshInstance> instances, const Camera2D& camera,
               bool visible_only = true);
    void clear() noexcept;

    [[nodiscard]] std::span<const StaticMeshDrawCommand> commands() const noexcept;
    [[nodiscard]] std::size_t maximum_instances() const noexcept;

  private:
    std::size_t maximum_instances_{};
    std::vector<StaticMeshDrawCommand> commands_;
};

} // namespace meat2d::render
