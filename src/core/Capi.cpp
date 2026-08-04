#include "meat2d/c_api.h"

#include "meat2d/sim/Material.hpp"
#include "meat2d/sim/World.hpp"

#include <limits>
#include <new>

struct meat2d_world {
    explicit meat2d_world(meat2d::WorldConfig config) : world(config) {}

    meat2d::World world;
};

namespace {

meat2d_status validate_world(const meat2d_world* world) noexcept {
    return world == nullptr ? MEAT2D_STATUS_INVALID_ARGUMENT : MEAT2D_STATUS_OK;
}

meat2d_status validate_position(const meat2d_world* world, std::int32_t x,
                                std::int32_t y) noexcept {
    if (const auto status = validate_world(world); status != MEAT2D_STATUS_OK) {
        return status;
    }
    return world->world.in_bounds({x, y}) ? MEAT2D_STATUS_OK : MEAT2D_STATUS_OUT_OF_BOUNDS;
}

bool valid_material(std::uint8_t value) noexcept {
    return value < static_cast<std::uint8_t>(meat2d::MaterialId::Count);
}

} // namespace

extern "C" {

std::uint32_t meat2d_c_api_version(void) {
    return MEAT2D_C_API_VERSION;
}

meat2d_world* meat2d_world_create(std::int32_t width, std::int32_t height, std::uint64_t seed,
                                  std::uint32_t sleep_after_ticks) {
    if (width <= 0 || height <= 0 || sleep_after_ticks == 0U ||
        sleep_after_ticks > std::numeric_limits<std::uint16_t>::max()) {
        return nullptr;
    }
    try {
        return new meat2d_world({
            .width = width,
            .height = height,
            .seed = seed,
            .sleep_after_ticks = static_cast<std::uint16_t>(sleep_after_ticks),
        });
    } catch (...) {
        return nullptr;
    }
}

void meat2d_world_destroy(meat2d_world* world) {
    delete world;
}

meat2d_status meat2d_world_get_dimensions(const meat2d_world* world, std::int32_t* width,
                                          std::int32_t* height) {
    if (validate_world(world) != MEAT2D_STATUS_OK || width == nullptr || height == nullptr) {
        return MEAT2D_STATUS_INVALID_ARGUMENT;
    }
    *width = world->world.width();
    *height = world->world.height();
    return MEAT2D_STATUS_OK;
}

meat2d_status meat2d_world_get_tick(const meat2d_world* world, std::uint64_t* tick) {
    if (validate_world(world) != MEAT2D_STATUS_OK || tick == nullptr) {
        return MEAT2D_STATUS_INVALID_ARGUMENT;
    }
    *tick = world->world.current_tick();
    return MEAT2D_STATUS_OK;
}

meat2d_status meat2d_world_get_state_hash(const meat2d_world* world, std::uint64_t* state_hash) {
    if (validate_world(world) != MEAT2D_STATUS_OK || state_hash == nullptr) {
        return MEAT2D_STATUS_INVALID_ARGUMENT;
    }
    *state_hash = world->world.state_hash();
    return MEAT2D_STATUS_OK;
}

meat2d_status meat2d_world_get_material(const meat2d_world* world, std::int32_t x, std::int32_t y,
                                        std::uint8_t* material) {
    if (material == nullptr) {
        return MEAT2D_STATUS_INVALID_ARGUMENT;
    }
    if (const auto status = validate_position(world, x, y); status != MEAT2D_STATUS_OK) {
        return status;
    }
    *material = static_cast<std::uint8_t>(world->world.material({x, y}));
    return MEAT2D_STATUS_OK;
}

meat2d_status meat2d_world_set_material(meat2d_world* world, std::int32_t x, std::int32_t y,
                                        std::uint8_t material) {
    if (!valid_material(material)) {
        return MEAT2D_STATUS_INVALID_MATERIAL;
    }
    if (const auto status = validate_position(world, x, y); status != MEAT2D_STATUS_OK) {
        return status;
    }
    return world->world.set_material({x, y}, static_cast<meat2d::MaterialId>(material))
               ? MEAT2D_STATUS_OK
               : MEAT2D_STATUS_INTERNAL_ERROR;
}

meat2d_status meat2d_world_step(meat2d_world* world, meat2d_tick_stats* stats) {
    if (validate_world(world) != MEAT2D_STATUS_OK || stats == nullptr) {
        return MEAT2D_STATUS_INVALID_ARGUMENT;
    }
    const auto result = world->world.step();
    *stats = {
        .tick = result.tick,
        .moved_cells = result.moved_cells,
        .reacted_cells = result.reacted_cells,
        .heat_transfers = result.heat_transfers,
        .changed_chunks = result.changed_chunks,
        .active_chunks = result.active_chunks,
        .sleeping_chunks = result.sleeping_chunks,
    };
    return MEAT2D_STATUS_OK;
}

meat2d_status meat2d_world_rasterize_rgba(const meat2d_world* world, std::uint8_t* destination,
                                          std::size_t destination_bytes) {
    if (validate_world(world) != MEAT2D_STATUS_OK || destination == nullptr) {
        return MEAT2D_STATUS_INVALID_ARGUMENT;
    }
    const auto width = static_cast<std::size_t>(world->world.width());
    const auto height = static_cast<std::size_t>(world->world.height());
    if (height != 0U && width > std::numeric_limits<std::size_t>::max() / height) {
        return MEAT2D_STATUS_INTERNAL_ERROR;
    }
    const auto pixels = width * height;
    if (pixels > std::numeric_limits<std::size_t>::max() / 4U ||
        destination_bytes < pixels * 4U) {
        return MEAT2D_STATUS_INVALID_ARGUMENT;
    }
    world->world.rasterize_rgba({destination, pixels * 4U});
    return MEAT2D_STATUS_OK;
}

} // extern "C"
