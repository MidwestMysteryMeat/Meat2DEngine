#include "meat2d/scene/Scene.hpp"
#include "meat2d/sim/World.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cstdio>

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
    meat2d::World world({.width = 160, .height = 90, .seed = 0x5445525241ULL});
    for (int y = 45; y < world.height(); ++y) {
        for (int x = 0; x < world.width(); ++x) {
            world.set_material({x, y}, y == 45 ? meat2d::MaterialId::Grass
                                               : meat2d::MaterialId::Dirt);
        }
    }
    meat2d::scene::Scene scene("survival");
    const auto player = scene.create_entity("Survivor");
    scene.add_transform(player, {.position = {80, 40}});
    scene.add_tag(player, "inventory-owner");
    bool running = true;
    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
                running = false;
            }
        }
        const auto* keys = SDL_GetKeyboardState(nullptr);
        auto& position = scene.find(player)->transform->position;
        position.x += (keys[SDL_SCANCODE_D] ? 1 : 0) - (keys[SDL_SCANCODE_A] ? 1 : 0);
        position.y += (keys[SDL_SCANCODE_S] ? 1 : 0) - (keys[SDL_SCANCODE_W] ? 1 : 0);
        position.x = std::clamp(position.x, 2, world.width() - 3);
        position.y = std::clamp(position.y, 2, world.height() - 1);
        if (keys[SDL_SCANCODE_SPACE]) {
            world.set_material(position, meat2d::MaterialId::Wood);
        }
        world.step();
        SDL_SetRenderDrawColor(renderer, 18, 28, 47, 255);
        SDL_RenderClear(renderer);
        for (int y = 0; y < world.height(); ++y) {
            for (int x = 0; x < world.width(); ++x) {
                const auto color = meat2d::material_definition(world.material({x, y})).color;
                if (color.a == 0) {
                    continue;
                }
                SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
                SDL_FRect cell{static_cast<float>(x * 8), static_cast<float>(y * 8), 8.0F, 8.0F};
                SDL_RenderFillRect(renderer, &cell);
            }
        }
        const auto world_position = scene.world_position(player);
        SDL_FRect player_rect{static_cast<float>(world_position.x * 8 - 4),
                              static_cast<float>(world_position.y * 8 - 12), 8.0F, 12.0F};
        SDL_SetRenderDrawColor(renderer, 244, 224, 132, 255);
        SDL_RenderFillRect(renderer, &player_rect);
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
