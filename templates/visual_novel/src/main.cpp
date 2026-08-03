#include "meat2d/input/Input.hpp"
#include "meat2d/scene/Scene.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <array>
#include <cstdint>
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

    meat2d::scene::Scene stage("dialogue");
    const auto backdrop = stage.create_entity("Backdrop");
    const auto speaker = stage.create_entity("Speaker");
    stage.add_tag(backdrop, "background");
    stage.add_tag(speaker, "portrait");
    std::array<const char*, 3> lines{"The signal is still alive.", "Then we follow it.",
                                     "Choose what happens next."};
    std::size_t line = 0;
    meat2d::input::InputState input;
    bool running = true;
    while (running) {
        input.begin_frame();
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                if (event.key.key == SDLK_ESCAPE) {
                    running = false;
                } else if (event.key.key == SDLK_SPACE || event.key.key == SDLK_RETURN) {
                    line = (line + 1U) % lines.size();
                }
            }
        }

        SDL_SetRenderDrawColor(renderer, 19, 23, 42, 255);
        SDL_RenderClear(renderer);
        SDL_FRect portrait{72.0F, 92.0F, 360.0F, 470.0F};
        SDL_SetRenderDrawColor(renderer, 74, 111, 170, 255);
        SDL_RenderFillRect(renderer, &portrait);
        SDL_FRect dialogue{48.0F, 570.0F, 1184.0F, 110.0F};
        SDL_SetRenderDrawColor(renderer, 8, 11, 22, 245);
        SDL_RenderFillRect(renderer, &dialogue);
        SDL_FRect progress{72.0F, 610.0F, 120.0F + static_cast<float>(line) * 80.0F, 6.0F};
        SDL_SetRenderDrawColor(renderer, 236, 190, 92, 255);
        SDL_RenderFillRect(renderer, &progress);
        SDL_RenderPresent(renderer);
        (void)lines[line]; // Replace this with the engine's font/UI adapter.
        SDL_Delay(16);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
