#include <meat2d/sim/World.hpp>

int main() {
    meat2d::World world({
        .width = 64,
        .height = 64,
        .seed = 1234,
        .sleep_after_ticks = 5,
    });
    world.set_material({32, 1}, meat2d::MaterialId::Sand);
    world.step();
    return world.current_tick() == 1 ? 0 : 1;
}
