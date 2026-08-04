#ifndef MEAT2D_C_API_H
#define MEAT2D_C_API_H

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define MEAT2D_C_API_VERSION UINT32_C(1)

typedef struct meat2d_world meat2d_world;

typedef enum meat2d_status {
    MEAT2D_STATUS_OK = 0,
    MEAT2D_STATUS_INVALID_ARGUMENT = 1,
    MEAT2D_STATUS_OUT_OF_BOUNDS = 2,
    MEAT2D_STATUS_INVALID_MATERIAL = 3,
    MEAT2D_STATUS_ALLOCATION_FAILED = 4,
    MEAT2D_STATUS_INTERNAL_ERROR = 5
} meat2d_status;

typedef struct meat2d_tick_stats {
    uint64_t tick;
    uint64_t moved_cells;
    uint64_t reacted_cells;
    uint64_t heat_transfers;
    uint32_t changed_chunks;
    uint32_t active_chunks;
    uint32_t sleeping_chunks;
} meat2d_tick_stats;

uint32_t meat2d_c_api_version(void);

meat2d_world* meat2d_world_create(int32_t width, int32_t height, uint64_t seed,
                                   uint32_t sleep_after_ticks);
void meat2d_world_destroy(meat2d_world* world);

meat2d_status meat2d_world_get_dimensions(const meat2d_world* world, int32_t* width,
                                          int32_t* height);
meat2d_status meat2d_world_get_tick(const meat2d_world* world, uint64_t* tick);
meat2d_status meat2d_world_get_state_hash(const meat2d_world* world, uint64_t* state_hash);
meat2d_status meat2d_world_get_material(const meat2d_world* world, int32_t x, int32_t y,
                                        uint8_t* material);
meat2d_status meat2d_world_set_material(meat2d_world* world, int32_t x, int32_t y,
                                        uint8_t material);
meat2d_status meat2d_world_step(meat2d_world* world, meat2d_tick_stats* stats);
meat2d_status meat2d_world_rasterize_rgba(const meat2d_world* world, uint8_t* destination,
                                          size_t destination_bytes);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif
