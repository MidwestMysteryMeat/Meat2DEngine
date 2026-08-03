#include <meat2d/ai/LivingSimulation.hpp>
#include <meat2d/core/FixedTimestep.hpp>
#include <meat2d/net/Session.hpp>
#include <meat2d/render/WorldView.hpp>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string_view>

namespace {

std::uint16_t parse_port(const char* text, std::uint16_t fallback) {
    std::uint16_t port = fallback;
    const auto result = std::from_chars(text, text + std::char_traits<char>::length(text), port);
    return result.ec == std::errc{} ? port : fallback;
}

bool passable(const meat2d::World& world, meat2d::Vec2i position) {
    if (!world.in_bounds(position)) {
        return false;
    }
    const auto phase = meat2d::material_definition(world.material(position)).phase;
    return phase == meat2d::MaterialPhase::Empty || phase == meat2d::MaterialPhase::Gas ||
           phase == meat2d::MaterialPhase::Liquid;
}

void seed_arena(meat2d::World& world) {
    for (int x = 0; x < world.width(); ++x) {
        world.set_material({x, 0}, meat2d::MaterialId::Concrete);
        world.set_material({x, world.height() - 1}, meat2d::MaterialId::Concrete);
    }
    for (int y = 0; y < world.height(); ++y) {
        world.set_material({0, y}, meat2d::MaterialId::Concrete);
        world.set_material({world.width() - 1, y}, meat2d::MaterialId::Concrete);
    }
    for (int x = 50; x < 270; ++x) {
        if (x % 37 > 8) {
            world.set_material({x, 65}, meat2d::MaterialId::Stone);
            world.set_material({world.width() - x, 125}, meat2d::MaterialId::Wood);
        }
    }
    world.paint_disc({80, 100}, 14, meat2d::MaterialId::Water);
    world.paint_disc({245, 90}, 10, meat2d::MaterialId::Oil);
    world.wake_all();
}

void mark_player(meat2d::render::WorldView& view, const meat2d::World& world,
                 meat2d::Vec2i position) {
    for (int y = position.y - 2; y <= position.y + 2; ++y) {
        for (int x = position.x - 2; x <= position.x + 2; ++x) {
            view.mark_overlay_cell(world, {x, y});
        }
    }
}

void draw_player(std::span<std::uint8_t> pixels, const meat2d::World& world,
                 meat2d::Vec2i position) {
    for (int y = position.y - 2; y <= position.y + 2; ++y) {
        for (int x = position.x - 2; x <= position.x + 2; ++x) {
            if (!world.in_bounds({x, y})) {
                continue;
            }
            const auto offset =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(world.width()) +
                 static_cast<std::size_t>(x)) *
                4U;
            pixels[offset] = 87;
            pixels[offset + 1U] = 225;
            pixels[offset + 2U] = 255;
            pixels[offset + 3U] = 255;
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }

    meat2d::net::AuthoritativeClient client;
    bool networked = argc >= 3 && std::string_view(argv[1]) == "--connect";
    if (networked && !client.connect(
                         {.address = argv[2],
                          .port = argc >= 4 ? parse_port(argv[3], meat2d::net::default_port)
                                            : meat2d::net::default_port},
                         "Top-down Commander")) {
        std::fprintf(stderr, "Connection start failed: %s\n", client.last_error().data());
        networked = false;
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
        .seed = 0x544F50444F574EULL,
        .sleep_after_ticks = 30,
    });
    seed_arena(game.world());
    meat2d::Vec2i player{160, 90};

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
    meat2d::core::FixedTimestep fixed_timestep;
    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
                running = false;
            }
        }

        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now - previous);
        previous = now;
        const auto fixed = fixed_timestep.advance(elapsed);
        for (auto step = 0U; step < fixed.steps; ++step) {
            if (networked) {
                client.update();
                if (client.connected()) {
                    client.set_focus(player);
                }
            }
            const auto* keys = SDL_GetKeyboardState(nullptr);
            const meat2d::Vec2i direction{
                (keys[SDL_SCANCODE_D] ? 1 : 0) - (keys[SDL_SCANCODE_A] ? 1 : 0),
                (keys[SDL_SCANCODE_S] ? 1 : 0) - (keys[SDL_SCANCODE_W] ? 1 : 0),
            };
            const meat2d::Vec2i horizontal{player.x + direction.x, player.y};
            if (direction.x != 0 && passable(game.world(), horizontal)) {
                player = horizontal;
            }
            const meat2d::Vec2i vertical{player.x, player.y + direction.y};
            if (direction.y != 0 && passable(game.world(), vertical)) {
                player = vertical;
            }

            float mouse_x = 0.0F;
            float mouse_y = 0.0F;
            const auto buttons = SDL_GetMouseState(&mouse_x, &mouse_y);
            if ((buttons & SDL_BUTTON_LMASK) != 0U) {
                int output_width = 1;
                int output_height = 1;
                SDL_GetRenderOutputSize(renderer, &output_width, &output_height);
                const meat2d::Vec2i target{
                    static_cast<int>(mouse_x * static_cast<float>(game.world().width()) /
                                     static_cast<float>(output_width)),
                    static_cast<int>(mouse_y * static_cast<float>(game.world().height()) /
                                     static_cast<float>(output_height)),
                };
                game.world().paint_disc(target, 3, meat2d::MaterialId::Fire);
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
        SDL_SetRenderDrawColor(renderer, 5, 8, 13, 255);
        SDL_RenderClear(renderer);
        SDL_RenderTexture(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);
        SDL_Delay(1);
    }

    SDL_DestroyTexture(texture);
    client.disconnect();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
