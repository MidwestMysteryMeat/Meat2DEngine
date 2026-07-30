#include <meat2d/ai/LivingSimulation.hpp>

int main() {
    meat2d::ai::LivingSimulation simulation({
        .width = 64,
        .height = 64,
        .seed = 1234,
        .sleep_after_ticks = 5,
    });
    simulation.world().set_material({32, 1}, meat2d::MaterialId::Sand);
    simulation.organisms().seed(
        {20, 20},
        meat2d::life::photosynthetic_genome);
    simulation.step();
    return simulation.world().current_tick() == 1 &&
                   simulation.organisms().population() > 0
               ? 0
               : 1;
}
