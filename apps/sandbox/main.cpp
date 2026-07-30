#include "meat2d/core/Version.hpp"
#include "meat2d/sim/Scenario.hpp"
#include "meat2d/sim/World.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr int simulation_hz = 60;
constexpr double fixed_seconds = 1.0 / simulation_hz;

struct Viewport {
    SDL_FRect destination{};
    float scale{1.0F};
};

Viewport calculate_viewport(SDL_Renderer* renderer, const meat2d::World& world) {
    int output_width = 1;
    int output_height = 1;
    SDL_GetRenderOutputSize(renderer, &output_width, &output_height);

    const float scale_x =
        static_cast<float>(output_width) / static_cast<float>(world.width());
    const float scale_y =
        static_cast<float>(output_height) / static_cast<float>(world.height());
    const float scale = std::max(1.0F, std::min(scale_x, scale_y));
    const float width = static_cast<float>(world.width()) * scale;
    const float height = static_cast<float>(world.height()) * scale;
    return {
        {
            (static_cast<float>(output_width) - width) * 0.5F,
            (static_cast<float>(output_height) - height) * 0.5F,
            width,
            height,
        },
        scale,
    };
}

meat2d::Vec2i mouse_to_world(float mouse_x, float mouse_y, const Viewport& viewport) {
    return {
        static_cast<std::int32_t>(
            std::floor((mouse_x - viewport.destination.x) / viewport.scale)),
        static_cast<std::int32_t>(
            std::floor((mouse_y - viewport.destination.y) / viewport.scale)),
    };
}

const char* material_name(meat2d::MaterialId id) {
    return meat2d::material_definition(id).name.data();
}

meat2d::MaterialId cycle_material(meat2d::MaterialId current, int direction) {
    const auto count = static_cast<int>(meat2d::material_count);
    const auto current_index = static_cast<int>(current);
    return static_cast<meat2d::MaterialId>((current_index + direction + count) % count);
}

std::uint64_t parse_frame_limit(int argc, char** argv) {
    std::uint64_t frame_limit = 0;
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::string_view(argv[index]) != "--frames") {
            continue;
        }
        const std::string_view text(argv[index + 1]);
        const auto result =
            std::from_chars(text.data(), text.data() + text.size(), frame_limit);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
            frame_limit = 0;
        }
    }
    return frame_limit;
}

} // namespace

int main(int argc, char** argv) {
    const auto frame_limit = parse_frame_limit(argc, argv);
    SDL_SetAppMetadata("Meat2D Sand Lab", meat2d::version_string.data(), "games.meat2d.sandbox");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer(
            "Meat2D Sand Lab",
            1280,
            720,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY,
            &window,
            &renderer)) {
        std::fprintf(stderr, "Window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    const meat2d::WorldConfig world_config{
        .width = 320,
        .height = 180,
        .seed = 0x4D4541543244ULL,
        .sleep_after_ticks = 30,
    };
    meat2d::World world(world_config);
    meat2d::seed_elements_lab(world);

    SDL_Texture* texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STREAMING,
        world.width(),
        world.height());
    if (texture == nullptr) {
        std::fprintf(stderr, "Texture creation failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);

    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(world.width()) *
        static_cast<std::size_t>(world.height()) * 4U);

    bool running = true;
    bool paused = false;
    bool single_step = false;
    int brush_radius = 5;
    meat2d::MaterialId brush = meat2d::MaterialId::Sand;
    meat2d::TickStats last_stats{};

    auto previous = std::chrono::steady_clock::now();
    auto title_update = previous;
    double accumulator = 0.0;
    std::uint64_t rendered_frames = 0;

    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                switch (event.key.key) {
                case SDLK_ESCAPE:
                    running = false;
                    break;
                case SDLK_SPACE:
                    paused = !paused;
                    break;
                case SDLK_N:
                    single_step = true;
                    break;
                case SDLK_0:
                    brush = meat2d::MaterialId::Empty;
                    break;
                case SDLK_1:
                    brush = meat2d::MaterialId::Sand;
                    break;
                case SDLK_2:
                    brush = meat2d::MaterialId::Water;
                    break;
                case SDLK_3:
                    brush = meat2d::MaterialId::Stone;
                    break;
                case SDLK_4:
                    brush = meat2d::MaterialId::Wood;
                    break;
                case SDLK_5:
                    brush = meat2d::MaterialId::Oil;
                    break;
                case SDLK_6:
                    brush = meat2d::MaterialId::Fire;
                    break;
                case SDLK_7:
                    brush = meat2d::MaterialId::Acid;
                    break;
                case SDLK_8:
                    brush = meat2d::MaterialId::Lava;
                    break;
                case SDLK_9:
                    brush = meat2d::MaterialId::Gunpowder;
                    break;
                case SDLK_Q:
                    brush = cycle_material(brush, -1);
                    break;
                case SDLK_E:
                    brush = cycle_material(brush, 1);
                    break;
                case SDLK_R:
                    world = meat2d::World(world_config);
                    meat2d::seed_elements_lab(world);
                    break;
                case SDLK_C:
                    world = meat2d::World(world_config);
                    world.wake_all();
                    break;
                default:
                    break;
                }
            } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                brush_radius = std::clamp(
                    brush_radius + static_cast<int>(event.wheel.y),
                    1,
                    32);
            }
        }

        const auto now = std::chrono::steady_clock::now();
        const double frame_seconds =
            std::chrono::duration<double>(now - previous).count();
        previous = now;
        accumulator += std::min(frame_seconds, 0.25);

        const auto viewport = calculate_viewport(renderer, world);
        float mouse_x = 0.0F;
        float mouse_y = 0.0F;
        const auto mouse_buttons = SDL_GetMouseState(&mouse_x, &mouse_y);
        const auto mouse_cell = mouse_to_world(mouse_x, mouse_y, viewport);
        if ((mouse_buttons & SDL_BUTTON_LMASK) != 0U) {
            world.paint_disc(mouse_cell, brush_radius, brush);
        } else if ((mouse_buttons & SDL_BUTTON_RMASK) != 0U) {
            world.paint_disc(mouse_cell, brush_radius, meat2d::MaterialId::Empty);
        }

        while ((accumulator >= fixed_seconds && !paused) || single_step) {
            last_stats = world.step();
            accumulator = std::max(0.0, accumulator - fixed_seconds);
            single_step = false;
            if (paused) {
                break;
            }
        }

        world.rasterize_rgba(pixels);
        SDL_UpdateTexture(texture, nullptr, pixels.data(), world.width() * 4);
        SDL_SetRenderDrawColor(renderer, 5, 7, 12, 255);
        SDL_RenderClear(renderer);
        SDL_RenderTexture(renderer, texture, nullptr, &viewport.destination);
        SDL_RenderPresent(renderer);
        ++rendered_frames;
        if (frame_limit != 0 && rendered_frames >= frame_limit) {
            running = false;
        }

        if (now - title_update >= std::chrono::milliseconds(250)) {
            title_update = now;
            const std::string title =
                "Meat2D Sand Lab | " + std::string(material_name(brush)) +
                " r=" + std::to_string(brush_radius) +
                (paused ? " | PAUSED" : "") +
                " | tick " + std::to_string(world.current_tick()) +
                " | moved " + std::to_string(last_stats.moved_cells) +
                " | reacted " + std::to_string(last_stats.reacted_cells) +
                " | heat " + std::to_string(last_stats.heat_transfers) +
                " | active chunks " + std::to_string(last_stats.active_chunks);
            SDL_SetWindowTitle(window, title.c_str());
        }

        SDL_Delay(1);
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
