#include "meat2d/input/Input.hpp"
#include "meat2d/scene/Scene.hpp"

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
    if (!SDL_CreateWindowAndRenderer("{{PROJECT_NAME}}", 960, 540, SDL_WINDOW_RESIZABLE,
                                     &window, &renderer)) {
        std::fprintf(stderr, "Window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    meat2d::scene::Scene scene("overworld");
    const auto hero = scene.create_entity("Hero");
    const auto shrine = scene.create_entity("Shrine");
    scene.add_tag(hero, "party-leader");
    scene.add_tag(shrine, "interactable");
    scene.add_transform(hero, {.position = {480, 260}});
    scene.add_transform(shrine, {.position = {700, 170}});

    bool running = true;
    bool encounter = false;
    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                if (event.key.key == SDLK_ESCAPE) {
                    running = false;
                } else if (event.key.key == SDLK_RETURN) {
                    encounter = !encounter;
                }
            }
        }
        const auto* keys = SDL_GetKeyboardState(nullptr);
        auto* transform = scene.find(hero)->transform ? &*scene.find(hero)->transform : nullptr;
        if (transform != nullptr && !encounter) {
            transform->position.x += (keys[SDL_SCANCODE_D] ? 2 : 0) - (keys[SDL_SCANCODE_A] ? 2 : 0);
            transform->position.y += (keys[SDL_SCANCODE_S] ? 2 : 0) - (keys[SDL_SCANCODE_W] ? 2 : 0);
            transform->position.x = std::clamp(transform->position.x, 40, 920);
            transform->position.y = std::clamp(transform->position.y, 40, 500);
        }
        SDL_SetRenderDrawColor(renderer, 22, 49, 38, 255);
        SDL_RenderClear(renderer);
        SDL_FRect hero_rect{static_cast<float>(scene.world_position(hero).x - 12),
                            static_cast<float>(scene.world_position(hero).y - 12), 24.0F, 24.0F};
        SDL_SetRenderDrawColor(renderer, 245, 216, 118, 255);
        SDL_RenderFillRect(renderer, &hero_rect);
        const auto shrine_position = scene.world_position(shrine);
        SDL_FRect shrine_rect{static_cast<float>(shrine_position.x - 18),
                              static_cast<float>(shrine_position.y - 18), 36.0F, 36.0F};
        SDL_SetRenderDrawColor(renderer, encounter ? 224 : 102, 126, 218, 255);
        SDL_RenderFillRect(renderer, &shrine_rect);
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
