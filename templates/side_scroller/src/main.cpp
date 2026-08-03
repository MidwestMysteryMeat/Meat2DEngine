#include <meat2d/ai/LivingSimulation.hpp>
#include <meat2d/core/FixedTimestep.hpp>
#include <meat2d/render/WorldView.hpp>
#include <meat2d/sim/Scenario.hpp>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <span>

namespace {

bool passable(const meat2d::World& world, meat2d::Vec2i position) {
    if (!world.in_bounds(position)) {
        return false;
    }
    const auto phase = meat2d::material_definition(world.material(position)).phase;
    return phase == meat2d::MaterialPhase::Empty || phase == meat2d::MaterialPhase::Gas ||
           phase == meat2d::MaterialPhase::Liquid;
}

void mark_player(meat2d::render::WorldView& view, const meat2d::World& world,
                 meat2d::Vec2i position) {
    for (int y = position.y - 4; y <= position.y; ++y) {
        for (int x = position.x - 1; x <= position.x + 1; ++x) {
            view.mark_overlay_cell(world, {x, y});
        }
    }
}

void draw_player(std::span<std::uint8_t> pixels, const meat2d::World& world,
                 meat2d::Vec2i position) {
    for (int y = position.y - 4; y <= position.y; ++y) {
        for (int x = position.x - 1; x <= position.x + 1; ++x) {
            if (!world.in_bounds({x, y})) {
                continue;
            }
            const auto offset =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(world.width()) +
                 static_cast<std::size_t>(x)) *
                4U;
            pixels[offset] = 255;
            pixels[offset + 1U] = 231;
            pixels[offset + 2U] = 112;
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
    if (!SDL_CreateWindowAndRenderer("{{PROJECT_NAME}}", 1280, 720, SDL_WINDOW_RESIZABLE, &window,
                                     &renderer)) {
        std::fprintf(stderr, "Window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    meat2d::ai::LivingSimulation game({
        .width = 320,
        .height = 180,
        .seed = 0x4D4541543244ULL,
        .sleep_after_ticks = 30,
    });
    meat2d::seed_elements_lab(game.world());
    auto player = meat2d::Vec2i{155, 150};
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
            const auto* keys = SDL_GetKeyboardState(nullptr);
            const int horizontal = (keys[SDL_SCANCODE_D] ? 1 : 0) - (keys[SDL_SCANCODE_A] ? 1 : 0);
            const meat2d::Vec2i horizontal_target{
                player.x + horizontal,
                player.y,
            };
            if (horizontal != 0 && passable(game.world(), horizontal_target)) {
                player = horizontal_target;
            }

            const meat2d::Vec2i below{player.x, player.y + 1};
            const bool grounded = !passable(game.world(), below);
            if (keys[SDL_SCANCODE_W] && grounded) {
                vertical_velocity = -3;
            } else {
                vertical_velocity = std::min(vertical_velocity + 1, 3);
            }
            const int vertical_step = (vertical_velocity > 0) - (vertical_velocity < 0);
            const meat2d::Vec2i vertical_target{
                player.x,
                player.y + vertical_step,
            };
            if (vertical_step != 0 && passable(game.world(), vertical_target)) {
                player = vertical_target;
            } else if (vertical_step != 0) {
                vertical_velocity = 0;
            }

            float mouse_x = 0.0F;
            float mouse_y = 0.0F;
            const auto buttons = SDL_GetMouseState(&mouse_x, &mouse_y);
            int output_width = 1;
            int output_height = 1;
            SDL_GetRenderOutputSize(renderer, &output_width, &output_height);
            const meat2d::Vec2i mouse_cell{
                static_cast<int>(mouse_x * static_cast<float>(game.world().width()) /
                                 static_cast<float>(output_width)),
                static_cast<int>(mouse_y * static_cast<float>(game.world().height()) /
                                 static_cast<float>(output_height)),
            };
            if ((buttons & SDL_BUTTON_LMASK) != 0U) {
                game.world().paint_disc(mouse_cell, 4, brush);
            } else if ((buttons & SDL_BUTTON_RMASK) != 0U) {
                game.world().paint_disc(mouse_cell, 4, meat2d::MaterialId::Empty);
            }

            game.step();
        }

        mark_player(view, game.world(), player);
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
