#include "meat2d/render/WorldView.hpp"
#include "meat2d/sim/Scenario.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cstdio>
#include <span>

namespace {

void draw_player(std::span<std::uint8_t> pixels, const meat2d::World& world,
                 meat2d::Vec2i player) {
    for (int y = player.y - 4; y <= player.y; ++y) {
        for (int x = player.x - 2; x <= player.x + 2; ++x) {
            if (!world.in_bounds({x, y})) {
                continue;
            }
            const auto offset =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(world.width()) +
                 static_cast<std::size_t>(x)) *
                4U;
            pixels[offset] = 210;
            pixels[offset + 1U] = 246;
            pixels[offset + 2U] = 132;
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
    meat2d::World world({.width = 320, .height = 180, .seed = 0x4E4F495441ULL});
    meat2d::seed_elements_lab(world);
    meat2d::Vec2i player{160, 120};
    auto* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                      SDL_TEXTUREACCESS_STREAMING, world.width(), world.height());
    if (texture == nullptr) {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    meat2d::render::WorldView view;
    bool running = true;
    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                       event.key.key == SDLK_SPACE) {
                world.paint_disc(player, 10, meat2d::MaterialId::Fire);
            }
        }
        const auto* keys = SDL_GetKeyboardState(nullptr);
        player.x += (keys[SDL_SCANCODE_D] ? 1 : 0) - (keys[SDL_SCANCODE_A] ? 1 : 0);
        player.y += (keys[SDL_SCANCODE_S] ? 1 : 0) - (keys[SDL_SCANCODE_W] ? 1 : 0);
        player.x = std::clamp(player.x, 2, world.width() - 3);
        player.y = std::clamp(player.y, 4, world.height() - 1);
        world.step();
        const auto frame = view.update(
            world, [&world, player](std::span<std::uint8_t> pixels, meat2d::RectI) {
                draw_player(pixels, world, player);
            });
        if (frame.full_upload) {
            SDL_UpdateTexture(texture, nullptr, view.pixels().data(), view.pitch_bytes());
        } else {
            for (const auto& region : frame.regions) {
                const SDL_Rect rect{region.x, region.y, region.width, region.height};
                SDL_UpdateTexture(texture, &rect, view.region_pixels(region), view.pitch_bytes());
            }
        }
        SDL_SetRenderDrawColor(renderer, 5, 8, 16, 255);
        SDL_RenderClear(renderer);
        SDL_RenderTexture(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
