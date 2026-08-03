#include "meat2d/sim/World.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cstdio>

namespace {

void draw_world(SDL_Renderer* renderer, const meat2d::World& world) {
    constexpr float cell_size = 8.0F;
    for (int y = 0; y < world.height(); ++y) {
        for (int x = 0; x < world.width(); ++x) {
            const auto color = meat2d::material_definition(world.material({x, y})).color;
            if (color.a == 0) {
                continue;
            }
            SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
            SDL_FRect cell{static_cast<float>(x) * cell_size, static_cast<float>(y) * cell_size,
                           cell_size, cell_size};
            SDL_RenderFillRect(renderer, &cell);
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
    meat2d::World world({.width = 160, .height = 90, .seed = 0x574F524D53ULL});
    for (int y = 50; y < world.height(); ++y) {
        for (int x = 0; x < world.width(); ++x) {
            world.set_material({x, y}, y == 50 ? meat2d::MaterialId::Grass
                                               : meat2d::MaterialId::Dirt);
        }
    }
    meat2d::Vec2i player{28, 45};
    bool player_two_turn = false;
    bool running = true;
    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                       event.key.key == SDLK_SPACE) {
                world.paint_disc({player.x + (player_two_turn ? -10 : 10), player.y + 4}, 8,
                                  meat2d::MaterialId::Empty);
                player_two_turn = !player_two_turn;
            }
        }
        const auto* keys = SDL_GetKeyboardState(nullptr);
        player.x += (keys[SDL_SCANCODE_D] ? 1 : 0) - (keys[SDL_SCANCODE_A] ? 1 : 0);
        player.x = std::clamp(player.x, 2, world.width() - 3);
        world.step();
        SDL_SetRenderDrawColor(renderer, 12, 16, 30, 255);
        SDL_RenderClear(renderer);
        draw_world(renderer, world);
        SDL_FRect player_rect{static_cast<float>(player.x * 8 - 4),
                              static_cast<float>(player.y * 8 - 12), 8.0F, 12.0F};
        SDL_SetRenderDrawColor(renderer, player_two_turn ? 245 : 112, 196,
                               player_two_turn ? 112 : 245, 255);
        SDL_RenderFillRect(renderer, &player_rect);
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
