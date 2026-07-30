// Deep Dig — a small complete game built on Meat2D Engine's simulation,
// rendering, and living-agent systems. Dig down through the mineshaft to
// reach the vault before the rising flood catches you, and stay clear of
// the predator hunting the shaft.

#include <meat2d/ai/LivingSimulation.hpp>
#include <meat2d/render/WorldView.hpp>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <span>
#include <string>

namespace {

constexpr double fixed_seconds = 1.0 / 60.0;
constexpr std::int32_t world_width = 120;
constexpr std::int32_t world_height = 170;
constexpr meat2d::Vec2i spawn_point{world_width / 2, 8};
constexpr std::int32_t vault_row = world_height - 10;
constexpr double move_cooldown_seconds = 0.06;
constexpr double dig_cooldown_seconds = 0.12;
constexpr double flood_grace_seconds = 4.0;
constexpr double flood_interval_seconds = 0.3;

enum class GameState : std::uint8_t { Playing, Won, Lost };

bool diggable(meat2d::MaterialId material) {
    switch (material) {
        case meat2d::MaterialId::Stone:
        case meat2d::MaterialId::Soil:
        case meat2d::MaterialId::Sand:
        case meat2d::MaterialId::Wood:
            return true;
        default:
            return false;
    }
}

bool walkable(const meat2d::World& world, meat2d::Vec2i position) {
    if (!world.in_bounds(position)) {
        return false;
    }
    const auto phase = meat2d::material_definition(world.material(position)).phase;
    return phase == meat2d::MaterialPhase::Empty || phase == meat2d::MaterialPhase::Gas;
}

void seed_mine(meat2d::World& world, std::mt19937_64& rng) {
    for (std::int32_t x = 0; x < world.width(); ++x) {
        world.set_material({x, 0}, meat2d::MaterialId::Concrete);
        world.set_material({x, world.height() - 1}, meat2d::MaterialId::Concrete);
    }
    for (std::int32_t y = 0; y < world.height(); ++y) {
        world.set_material({0, y}, meat2d::MaterialId::Concrete);
        world.set_material({world.width() - 1, y}, meat2d::MaterialId::Concrete);
    }
    for (std::int32_t y = 1; y < world.height() - 1; ++y) {
        for (std::int32_t x = 1; x < world.width() - 1; ++x) {
            world.set_material({x, y}, meat2d::MaterialId::Stone);
        }
    }

    std::uniform_int_distribution<std::int32_t> pocket_x(4, world.width() - 5);
    std::uniform_int_distribution<std::int32_t> pocket_y(vault_row - 4, world.height() - 3);
    std::uniform_int_distribution<std::int32_t> radius(2, 4);
    for (std::int32_t index = 0; index < 40; ++index) {
        world.paint_disc({pocket_x(rng), pocket_y(rng)}, radius(rng), meat2d::MaterialId::Soil);
    }
    for (std::int32_t index = 0; index < 10; ++index) {
        world.paint_disc({pocket_x(rng), pocket_y(rng)}, 3, meat2d::MaterialId::Water);
    }

    for (std::int32_t y = spawn_point.y - 3; y <= spawn_point.y + 3; ++y) {
        for (std::int32_t x = spawn_point.x - 3; x <= spawn_point.x + 3; ++x) {
            world.set_material({x, y}, meat2d::MaterialId::Empty);
        }
    }

    for (std::int32_t x = world_width / 2 - 6; x <= world_width / 2 + 6; ++x) {
        world.set_material({x, vault_row}, meat2d::MaterialId::Metal);
        world.set_material({x, world.height() - 3}, meat2d::MaterialId::Metal);
    }
    for (std::int32_t y = vault_row; y <= world.height() - 3; ++y) {
        world.set_material({world_width / 2 - 6, y}, meat2d::MaterialId::Metal);
        world.set_material({world_width / 2 + 6, y}, meat2d::MaterialId::Metal);
    }
    for (std::int32_t y = vault_row + 1; y < world.height() - 3; ++y) {
        for (std::int32_t x = world_width / 2 - 5; x <= world_width / 2 + 5; ++x) {
            world.set_material({x, y}, meat2d::MaterialId::Empty);
        }
    }
    for (std::int32_t x = world_width / 2 - 2; x <= world_width / 2 + 2; ++x) {
        world.set_material({x, vault_row}, meat2d::MaterialId::Empty);
    }

    world.wake_all();
}

void draw_glyph(std::span<std::uint8_t> pixels, const meat2d::World& world, meat2d::Vec2i center,
                std::int32_t radius, meat2d::Rgba8 color) {
    for (std::int32_t y = center.y - radius; y <= center.y + radius; ++y) {
        for (std::int32_t x = center.x - radius; x <= center.x + radius; ++x) {
            if (!world.in_bounds({x, y})) {
                continue;
            }
            const auto offset =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(world.width()) +
                 static_cast<std::size_t>(x)) *
                4U;
            pixels[offset] = color.r;
            pixels[offset + 1U] = color.g;
            pixels[offset + 2U] = color.b;
            pixels[offset + 3U] = color.a;
        }
    }
}

void mark_glyph(meat2d::render::WorldView& view, const meat2d::World& world, meat2d::Vec2i center,
                std::int32_t radius) {
    for (std::int32_t y = center.y - radius; y <= center.y + radius; ++y) {
        for (std::int32_t x = center.x - radius; x <= center.x + radius; ++x) {
            view.mark_overlay_cell(world, {x, y});
        }
    }
}

std::int32_t chebyshev_distance(meat2d::Vec2i first, meat2d::Vec2i second) {
    return std::max(std::abs(first.x - second.x), std::abs(first.y - second.y));
}

struct RunState {
    meat2d::ai::LivingSimulation simulation;
    meat2d::Vec2i player{spawn_point};
    GameState state{GameState::Playing};
    double move_timer{};
    double dig_timer{};
    double flood_timer{};
    std::int32_t flood_row{4};
    double survived_seconds{};
    meat2d::ai::EntityId predator_id{};

    explicit RunState(std::mt19937_64& rng)
        : simulation({
              .width = world_width,
              .height = world_height,
              .seed = rng(),
              .sleep_after_ticks = 30,
          }) {
        seed_mine(simulation.world(), rng);
        simulation.spawn_agent(meat2d::ai::AgentKind::Grazer, {world_width / 4, world_height / 2});
        predator_id = simulation.spawn_agent(
            meat2d::ai::AgentKind::Predator, {3 * world_width / 4, world_height / 2});
    }
};

} // namespace

int main(int, char**) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("Deep Dig — a Meat2D Engine example", 960, 1080,
                                     SDL_WINDOW_RESIZABLE, &window, &renderer)) {
        std::fprintf(stderr, "Window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    std::mt19937_64 rng{std::random_device{}()};
    RunState run(rng);

    auto* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                      world_width, world_height);
    if (texture == nullptr) {
        std::fprintf(stderr, "Texture creation failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    meat2d::render::WorldView view;

    bool running = true;
    auto previous = std::chrono::steady_clock::now();
    double accumulator = 0.0;
    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
            if (event.type == SDL_EVENT_KEY_DOWN) {
                if (event.key.key == SDLK_ESCAPE) {
                    running = false;
                }
                if (event.key.key == SDLK_R && run.state != GameState::Playing) {
                    run = RunState(rng);
                    view.invalidate();
                }
            }
        }

        const auto now = std::chrono::steady_clock::now();
        accumulator += std::min(0.25, std::chrono::duration<double>(now - previous).count());
        previous = now;

        while (accumulator >= fixed_seconds) {
            accumulator -= fixed_seconds;
            if (run.state != GameState::Playing) {
                continue;
            }
            run.survived_seconds += fixed_seconds;
            run.move_timer += fixed_seconds;
            run.dig_timer += fixed_seconds;
            run.flood_timer += fixed_seconds;

            const auto* keys = SDL_GetKeyboardState(nullptr);
            const meat2d::Vec2i direction{
                (keys[SDL_SCANCODE_D] ? 1 : 0) - (keys[SDL_SCANCODE_A] ? 1 : 0),
                (keys[SDL_SCANCODE_S] ? 1 : 0) - (keys[SDL_SCANCODE_W] ? 1 : 0),
            };
            if (direction.x != 0 || direction.y != 0) {
                const meat2d::Vec2i target{run.player.x + direction.x, run.player.y + direction.y};
                auto& world = run.simulation.world();
                if (world.in_bounds(target)) {
                    if (walkable(world, target) && run.move_timer >= move_cooldown_seconds) {
                        run.player = target;
                        run.move_timer = 0.0;
                    } else if (diggable(world.material(target)) &&
                              run.dig_timer >= dig_cooldown_seconds) {
                        world.set_material(target, meat2d::MaterialId::Empty);
                        run.player = target;
                        run.dig_timer = 0.0;
                        run.move_timer = 0.0;
                    }
                }
            }

            if (run.survived_seconds >= flood_grace_seconds &&
                run.flood_timer >= flood_interval_seconds && run.flood_row < vault_row) {
                run.flood_timer = 0.0;
                ++run.flood_row;
                auto& world = run.simulation.world();
                for (std::int32_t x = 1; x < world.width() - 1; ++x) {
                    world.set_material({x, run.flood_row}, meat2d::MaterialId::Lava);
                }
                world.wake_all();
            }

            run.simulation.step();

            if (run.player.y <= run.flood_row) {
                run.state = GameState::Lost;
            }
            if (const auto* predator = run.simulation.find_agent(run.predator_id);
                predator != nullptr && predator->health > 0U &&
                chebyshev_distance(predator->position, run.player) <= 1) {
                run.state = GameState::Lost;
            }
            if (run.player.y >= vault_row) {
                run.state = GameState::Won;
            }
        }

        auto& world = run.simulation.world();
        mark_glyph(view, world, run.player, 1);
        for (const auto& agent : run.simulation.agents()) {
            mark_glyph(view, world, agent.position, 1);
        }
        for (std::int32_t x = 1; x < world.width() - 1; ++x) {
            view.mark_overlay_cell(world, {x, run.flood_row});
        }

        const auto frame = view.update(world, [&run](std::span<std::uint8_t> pixels, meat2d::RectI) {
            for (const auto& agent : run.simulation.agents()) {
                const auto color = agent.kind == meat2d::ai::AgentKind::Predator
                                       ? meat2d::Rgba8{235, 64, 52, 255}
                                       : meat2d::Rgba8{120, 220, 130, 255};
                draw_glyph(pixels, run.simulation.world(), agent.position, 1, color);
            }
            draw_glyph(pixels, run.simulation.world(), run.player, 1, {87, 225, 255, 255});
        });
        if (frame.full_upload) {
            SDL_UpdateTexture(texture, nullptr, view.pixels().data(), view.pitch_bytes());
        } else {
            for (const auto& region : frame.regions) {
                const SDL_Rect update_rect{region.x, region.y, region.width, region.height};
                SDL_UpdateTexture(texture, &update_rect, view.region_pixels(region),
                                  view.pitch_bytes());
            }
        }

        const auto background = run.state == GameState::Won
                                    ? SDL_Color{10, 40, 15, 255}
                                    : run.state == GameState::Lost
                                          ? SDL_Color{40, 10, 10, 255}
                                          : SDL_Color{5, 6, 10, 255};
        SDL_SetRenderDrawColor(renderer, background.r, background.g, background.b, background.a);
        SDL_RenderClear(renderer);
        SDL_RenderTexture(renderer, texture, nullptr, nullptr);

        SDL_SetRenderDrawColor(renderer, 235, 235, 240, 255);
        const auto depth = std::max<std::int32_t>(0, run.player.y - spawn_point.y);
        char hud[192];
        std::snprintf(hud, sizeof(hud), "DEEP DIG  depth %d  flood row %d  time %.1fs", depth,
                     run.flood_row, run.survived_seconds);
        SDL_RenderDebugText(renderer, 12, 12, hud);
        if (run.state == GameState::Won) {
            SDL_RenderDebugText(renderer, 12, 28, "VAULT REACHED — press R to dig again");
        } else if (run.state == GameState::Lost) {
            SDL_RenderDebugText(renderer, 12, 28, "LOST — press R to try again");
        } else {
            SDL_RenderDebugText(renderer, 12, 28, "WASD to move/dig. Reach the vault below the flood.");
        }
        SDL_RenderPresent(renderer);
        SDL_Delay(1);
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
