#include "meat2d/scene/Physics.hpp"

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
    meat2d::scene::Scene scene("castle-room");
    const auto hero = scene.create_entity("Hunter");
    const auto floor = scene.create_entity("Floor");
    scene.add_transform(hero, {.position = {160, 420}});
    scene.add_collider(hero, {.bounds = {0, 0, 24, 42}});
    scene.add_rigid_body(hero, {.max_velocity = {6, 12}});
    scene.add_transform(floor, {.position = {0, 500}});
    scene.add_collider(floor, {.bounds = {0, 0, 1280, 40}, .category_bits = 2});
    scene.add_tag(hero, "player");
    scene.add_tag(floor, "terrain");

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
        auto* body = scene.find(hero)->rigid_body ? &*scene.find(hero)->rigid_body : nullptr;
        if (body != nullptr) {
            body->acceleration.x = (keys[SDL_SCANCODE_D] ? 1 : 0) -
                                   (keys[SDL_SCANCODE_A] ? 1 : 0);
            if (keys[SDL_SCANCODE_SPACE] && scene.find(hero)->transform->position.y >= 458) {
                body->velocity.y = -10;
            }
        }
        meat2d::scene::step_rigid_bodies(scene, {0, 1});
        const auto position = scene.world_position(hero);
        SDL_SetRenderDrawColor(renderer, 18, 14, 32, 255);
        SDL_RenderClear(renderer);
        SDL_FRect hero_rect{static_cast<float>(position.x), static_cast<float>(position.y - 42),
                            24.0F, 42.0F};
        SDL_SetRenderDrawColor(renderer, 206, 72, 92, 255);
        SDL_RenderFillRect(renderer, &hero_rect);
        SDL_FRect floor_rect{0.0F, 500.0F, 1280.0F, 40.0F};
        SDL_SetRenderDrawColor(renderer, 90, 83, 112, 255);
        SDL_RenderFillRect(renderer, &floor_rect);
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
