#include <meat2d/ai/LivingSimulation.hpp>
#include <meat2d/core/FixedTimestep.hpp>
#include <meat2d/render/WorldView.hpp>
#include <meat2d/sim/Scenario.hpp>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <chrono>
#include <cstdio>
#include <span>

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
        .seed = 0x46414C4C494E47ULL,
        .sleep_after_ticks = 30,
    });
    meat2d::seed_elements_lab(game.world());
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
    meat2d::MaterialId brush = meat2d::MaterialId::Sand;
    bool running = true;
    auto previous = std::chrono::steady_clock::now();
    meat2d::core::FixedTimestep fixed_timestep;

    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                if (event.key.key == SDLK_ESCAPE) {
                    running = false;
                } else if (event.key.key == SDLK_1) {
                    brush = meat2d::MaterialId::Sand;
                } else if (event.key.key == SDLK_2) {
                    brush = meat2d::MaterialId::Water;
                } else if (event.key.key == SDLK_3) {
                    brush = meat2d::MaterialId::Stone;
                } else if (event.key.key == SDLK_4) {
                    brush = meat2d::MaterialId::Fire;
                }
            }
        }

        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now - previous);
        previous = now;
        const auto fixed = fixed_timestep.advance(elapsed);
        for (auto step = 0U; step < fixed.steps; ++step) {
            float mouse_x = 0.0F;
            float mouse_y = 0.0F;
            const auto buttons = SDL_GetMouseState(&mouse_x, &mouse_y);
            int output_width = 1;
            int output_height = 1;
            SDL_GetRenderOutputSize(renderer, &output_width, &output_height);
            const meat2d::Vec2i target{
                static_cast<int>(mouse_x * static_cast<float>(game.world().width()) /
                                 static_cast<float>(output_width)),
                static_cast<int>(mouse_y * static_cast<float>(game.world().height()) /
                                 static_cast<float>(output_height)),
            };
            if ((buttons & SDL_BUTTON_LMASK) != 0U) {
                game.world().paint_disc(target, 4, brush);
            } else if ((buttons & SDL_BUTTON_RMASK) != 0U) {
                game.world().paint_disc(target, 4, meat2d::MaterialId::Empty);
            }
            game.step();
        }

        const auto frame = view.update(game.world());
        if (frame.full_upload) {
            SDL_UpdateTexture(texture, nullptr, view.pixels().data(), view.pitch_bytes());
        } else {
            for (const auto& region : frame.regions) {
                const SDL_Rect update_rect{region.x, region.y, region.width, region.height};
                SDL_UpdateTexture(texture, &update_rect, view.region_pixels(region),
                                  view.pitch_bytes());
            }
        }
        SDL_SetRenderDrawColor(renderer, 8, 10, 18, 255);
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
