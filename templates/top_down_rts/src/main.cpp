#include "meat2d/net/Session.hpp"
#include "meat2d/scene/Scene.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <charconv>
#include <cstdio>
#include <string_view>

namespace {

std::uint16_t parse_port(const char* text, std::uint16_t fallback) {
    std::uint16_t port = fallback;
    const auto result = std::from_chars(text, text + std::char_traits<char>::length(text), port);
    return result.ec == std::errc{} ? port : fallback;
}

} // namespace

int main(int argc, char** argv) {
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
    meat2d::net::AuthoritativeClient client;
    bool networked = argc >= 3 && std::string_view(argv[1]) == "--connect";
    if (networked && !client.connect({.address = argv[2],
                                      .port = argc >= 4 ? parse_port(argv[3], meat2d::net::default_port)
                                                        : meat2d::net::default_port},
                                     "RTS Commander")) {
        std::fprintf(stderr, "Connection start failed: %s\n", client.last_error().data());
        networked = false;
    }

    meat2d::scene::Scene scene("rts-map");
    const auto base = scene.create_entity("Base");
    const auto unit = scene.create_entity("Scout");
    scene.add_transform(base, {.position = {160, 360}});
    scene.add_transform(unit, {.position = {260, 340}});
    scene.add_tag(base, "faction-blue");
    scene.add_tag(unit, "selectable");
    bool running = true;
    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)) {
                running = false;
            }
        }
        if (networked) {
            client.update();
            if (client.connected()) {
                client.set_focus(scene.world_position(unit));
            }
        }
        const auto* keys = SDL_GetKeyboardState(nullptr);
        if (!networked) {
            auto& position = scene.find(unit)->transform->position;
            position.x += (keys[SDL_SCANCODE_D] ? 2 : 0) - (keys[SDL_SCANCODE_A] ? 2 : 0);
            position.y += (keys[SDL_SCANCODE_S] ? 2 : 0) - (keys[SDL_SCANCODE_W] ? 2 : 0);
        }
        SDL_SetRenderDrawColor(renderer, 25, 36, 50, 255);
        SDL_RenderClear(renderer);
        const auto base_position = scene.world_position(base);
        SDL_FRect base_rect{static_cast<float>(base_position.x - 28),
                            static_cast<float>(base_position.y - 28), 56.0F, 56.0F};
        SDL_SetRenderDrawColor(renderer, 64, 128, 220, 255);
        SDL_RenderFillRect(renderer, &base_rect);
        const auto unit_position = scene.world_position(unit);
        SDL_FRect unit_rect{static_cast<float>(unit_position.x - 10),
                            static_cast<float>(unit_position.y - 10), 20.0F, 20.0F};
        SDL_SetRenderDrawColor(renderer, networked ? 102 : 236, 218, 112, 255);
        SDL_RenderFillRect(renderer, &unit_rect);
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }
    client.disconnect();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
