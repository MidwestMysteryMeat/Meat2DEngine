#include <meat2d/ai/LivingSimulation.hpp>
#include <meat2d/render/WorldView.hpp>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <span>

namespace {

constexpr double fixed_seconds = 1.0 / 60.0;

bool passable(const meat2d::World& world, meat2d::Vec2i position) {
    if (!world.in_bounds(position)) {
        return false;
    }
    const auto phase = meat2d::material_definition(world.material(position)).phase;
    return phase == meat2d::MaterialPhase::Empty || phase == meat2d::MaterialPhase::Gas ||
           phase == meat2d::MaterialPhase::Liquid;
}

void seed_room(meat2d::World& world) {
    for (int x = 0; x < world.width(); ++x) {
        world.set_material({x, world.height() - 12}, meat2d::MaterialId::Stone);
        if (x > 32 && x < 96) {
            world.set_material({x, world.height() - 42}, meat2d::MaterialId::Stone);
        }
        if (x > 170 && x < 246) {
            world.set_material({x, world.height() - 72}, meat2d::MaterialId::Stone);
        }
    }
    world.wake_all();
}

void draw_player(std::span<std::uint8_t> pixels, const meat2d::World& world,
                 meat2d::Vec2i position) {
    for (int y = position.y - 5; y <= position.y; ++y) {
        for (int x = position.x - 2; x <= position.x + 2; ++x) {
            if (!world.in_bounds({x, y})) {
                continue;
            }
            const auto offset =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(world.width()) +
                 static_cast<std::size_t>(x)) *
                4U;
            pixels[offset] = 255;
            pixels[offset + 1U] = 184;
            pixels[offset + 2U] = 92;
            pixels[offset + 3U] = 255;
        }
    }
}

} // namespace

int main(int, char**) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("{{PROJECT_NAME}}", 1280, 720, SDL_WINDOW_RESIZABLE,
                                     &window, &renderer)) {
        std::fprintf(stderr, "Window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    meat2d::ai::LivingSimulation game({
        .width = 320,
        .height = 180,
        .seed = 0x4D4554524F4944ULL,
        .sleep_after_ticks = 30,
    });
    seed_room(game.world());
    meat2d::Vec2i player{24, 120};
    int vertical_velocity = 0;

    auto* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                      game.world().width(), game.world().height());
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
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
                running = false;
            }
        }
        const auto now = std::chrono::steady_clock::now();
        accumulator += std::min(0.25, std::chrono::duration<double>(now - previous).count());
        previous = now;
        while (accumulator >= fixed_seconds) {
            const auto* keys = SDL_GetKeyboardState(nullptr);
            const int horizontal = (keys[SDL_SCANCODE_D] ? 1 : 0) - (keys[SDL_SCANCODE_A] ? 1 : 0);
            const meat2d::Vec2i horizontal_target{player.x + horizontal, player.y};
            if (horizontal != 0 && passable(game.world(), horizontal_target)) {
                player = horizontal_target;
            }
            const bool grounded = !passable(game.world(), {player.x, player.y + 1});
            if (keys[SDL_SCANCODE_W] && grounded) {
                vertical_velocity = -4;
            } else {
                vertical_velocity = std::min(vertical_velocity + 1, 4);
            }
            const int vertical_step = (vertical_velocity > 0) - (vertical_velocity < 0);
            const meat2d::Vec2i vertical_target{player.x, player.y + vertical_step};
            if (vertical_step != 0 && passable(game.world(), vertical_target)) {
                player = vertical_target;
            } else if (vertical_step != 0) {
                vertical_velocity = 0;
            }
            game.step();
            accumulator -= fixed_seconds;
        }

        const auto frame = view.update(
            game.world(), [&game, player](std::span<std::uint8_t> pixels, meat2d::RectI) {
                draw_player(pixels, game.world(), player);
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
        SDL_SetRenderDrawColor(renderer, 12, 10, 26, 255);
        SDL_RenderClear(renderer);
        SDL_RenderTexture(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);
        SDL_Delay(1);
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
