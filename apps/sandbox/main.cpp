#include "meat2d/ai/LivingSimulation.hpp"
#include "meat2d/core/Version.hpp"
#include "meat2d/net/Session.hpp"
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

struct LaunchOptions {
    std::uint64_t frame_limit{};
    std::uint16_t port{meat2d::net::default_port};
    std::string host;
    std::string player_name{"Living Lab"};
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

template <typename Integer>
void parse_integer(std::string_view text, Integer& output) {
    const auto result =
        std::from_chars(text.data(), text.data() + text.size(), output);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
        output = 0;
    }
}

LaunchOptions parse_options(int argc, char** argv) {
    LaunchOptions options{};
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--frames" && index + 1 < argc) {
            parse_integer(std::string_view(argv[++index]), options.frame_limit);
        } else if (argument == "--connect" && index + 1 < argc) {
            options.host = argv[++index];
        } else if (argument == "--port" && index + 1 < argc) {
            parse_integer(std::string_view(argv[++index]), options.port);
        } else if (argument == "--name" && index + 1 < argc) {
            options.player_name = argv[++index];
        }
    }
    return options;
}

SDL_Texture* create_world_texture(
    SDL_Renderer* renderer,
    std::int32_t width,
    std::int32_t height) {
    auto* texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STREAMING,
        width,
        height);
    if (texture != nullptr) {
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
    }
    return texture;
}

void seed_living_lab(meat2d::ai::LivingSimulation& simulation) {
    auto& world = simulation.world();
    meat2d::seed_elements_lab(world);

    const auto floor_y = world.height() - std::max(4, world.height() / 18);
    for (int x = world.width() / 4; x < world.width() * 2 / 5; x += 12) {
        world.set_material({x, floor_y - 1}, meat2d::MaterialId::Plant);
    }
    for (int x = world.width() * 7 / 10; x < world.width() * 4 / 5; x += 4) {
        world.set_material({x, floor_y - 1}, meat2d::MaterialId::Debris);
    }

    simulation.spawn_agent(
        meat2d::ai::AgentKind::Grazer,
        {world.width() / 4 - 7, floor_y - 1});
    simulation.spawn_agent(
        meat2d::ai::AgentKind::Grazer,
        {world.width() / 3, floor_y - 2});
    simulation.spawn_agent(
        meat2d::ai::AgentKind::Predator,
        {world.width() / 2 - 12, floor_y - 1});
    simulation.spawn_agent(
        meat2d::ai::AgentKind::Worker,
        {world.width() * 2 / 3, floor_y - 1});

    for (std::int32_t y = floor_y - 8; y <= floor_y - 4; ++y) {
        for (std::int32_t x = world.width() / 10; x <= world.width() / 10 + 6; ++x) {
            simulation.organisms().seed(
                {x, y},
                meat2d::life::photosynthetic_genome,
                1'100);
        }
    }
    for (std::int32_t y = floor_y - 5; y <= floor_y - 2; ++y) {
        for (std::int32_t x = world.width() / 3; x <= world.width() / 3 + 5; ++x) {
            simulation.organisms().seed(
                {x, y},
                meat2d::life::decomposer_genome,
                1'100);
        }
    }
}

meat2d::Rgba8 agent_color(meat2d::ai::AgentKind kind) {
    switch (kind) {
    case meat2d::ai::AgentKind::Grazer:
        return {93, 238, 115, 255};
    case meat2d::ai::AgentKind::Predator:
        return {255, 62, 72, 255};
    case meat2d::ai::AgentKind::Worker:
        return {255, 207, 64, 255};
    }
    return {255, 255, 255, 255};
}

void rasterize_agents(
    const meat2d::ai::LivingSimulation& simulation,
    std::vector<std::uint8_t>& pixels) {
    const auto& world = simulation.world();
    for (const auto& agent : simulation.agents()) {
        const auto color = agent_color(agent.kind);
        for (std::int32_t y = agent.position.y - 1; y <= agent.position.y + 1; ++y) {
            for (std::int32_t x = agent.position.x - 1; x <= agent.position.x + 1; ++x) {
                if (!world.in_bounds({x, y})) {
                    continue;
                }
                const auto offset =
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(world.width()) +
                     static_cast<std::size_t>(x)) *
                    4U;
                pixels[offset] = color.r;
                pixels[offset + 1U] = color.g;
                pixels[offset + 2U] = color.b;
                pixels[offset + 3U] = color.a;
            }
        }
    }
}

void rasterize_organisms(
    const meat2d::ai::LivingSimulation& simulation,
    std::vector<std::uint8_t>& pixels) {
    const auto& field = simulation.organisms();
    const auto cells = field.cells();
    for (std::int32_t y = 0; y < field.height(); ++y) {
        for (std::int32_t x = 0; x < field.width(); ++x) {
            const auto index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(field.width()) +
                static_cast<std::size_t>(x);
            const auto& organism = cells[index];
            if (organism.genome == 0U) {
                continue;
            }
            const auto color = field.color(organism);
            const auto pixel = index * 4U;
            pixels[pixel] =
                static_cast<std::uint8_t>((static_cast<int>(pixels[pixel]) + color.r * 2) / 3);
            pixels[pixel + 1U] = static_cast<std::uint8_t>(
                (static_cast<int>(pixels[pixel + 1U]) + color.g * 2) / 3);
            pixels[pixel + 2U] = static_cast<std::uint8_t>(
                (static_cast<int>(pixels[pixel + 2U]) + color.b * 2) / 3);
        }
    }
}

const char* organism_name(std::uint32_t genome) {
    if (genome == meat2d::life::photosynthetic_genome) {
        return "photosynthetic";
    }
    if (genome == meat2d::life::decomposer_genome) {
        return "decomposer";
    }
    return "extremophile";
}

void seed_organism_brush(
    meat2d::life::OrganismField& field,
    meat2d::Vec2i center,
    int radius,
    std::uint32_t genome) {
    const auto radius_squared = radius * radius;
    for (std::int32_t y = center.y - radius; y <= center.y + radius; ++y) {
        for (std::int32_t x = center.x - radius; x <= center.x + radius; ++x) {
            const auto dx = x - center.x;
            const auto dy = y - center.y;
            if (dx * dx + dy * dy <= radius_squared) {
                field.seed({x, y}, genome, 1'100);
            }
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    const auto options = parse_options(argc, argv);
    SDL_SetAppMetadata(
        "Meat2D Living Lab",
        meat2d::version_string.data(),
        "games.meat2d.sandbox");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer(
            "Meat2D Living Lab",
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
    meat2d::ai::LivingSimulation simulation(world_config);
    seed_living_lab(simulation);
    meat2d::net::AuthoritativeClient remote;
    const bool remote_mode = !options.host.empty();
    if (remote_mode &&
        !remote.connect(
            {
                .address = options.host,
                .port = options.port,
            },
            options.player_name)) {
        std::fprintf(
            stderr,
            "Network connection start failed: %s\n",
            std::string(remote.last_error()).c_str());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    auto texture_width = simulation.world().width();
    auto texture_height = simulation.world().height();

    SDL_Texture* texture =
        create_world_texture(renderer, texture_width, texture_height);
    if (texture == nullptr) {
        std::fprintf(stderr, "Texture creation failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(texture_width) *
        static_cast<std::size_t>(texture_height) * 4U);

    bool running = true;
    bool paused = false;
    bool single_step = false;
    int brush_radius = 5;
    meat2d::MaterialId brush = meat2d::MaterialId::Sand;
    std::uint32_t organism_brush = meat2d::life::photosynthetic_genome;
    meat2d::ai::LivingStats last_stats{};
    meat2d::net::ClientUpdateStats last_network_stats{};
    std::uint64_t network_steps = 0;

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
                case SDLK_Z:
                    organism_brush = meat2d::life::photosynthetic_genome;
                    break;
                case SDLK_X:
                    organism_brush = meat2d::life::decomposer_genome;
                    break;
                case SDLK_V:
                    organism_brush = meat2d::life::extremophile_genome;
                    break;
                case SDLK_R:
                    if (!remote_mode) {
                        simulation = meat2d::ai::LivingSimulation(world_config);
                        seed_living_lab(simulation);
                    }
                    break;
                case SDLK_C:
                    if (!remote_mode) {
                        simulation = meat2d::ai::LivingSimulation(world_config);
                        simulation.world().wake_all();
                    }
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

        const meat2d::World* displayed_world =
            remote_mode && remote.replicated_world() != nullptr
                ? remote.replicated_world()
                : &simulation.world();
        const auto viewport = calculate_viewport(renderer, *displayed_world);
        float mouse_x = 0.0F;
        float mouse_y = 0.0F;
        const auto mouse_buttons = SDL_GetMouseState(&mouse_x, &mouse_y);
        const auto mouse_cell = mouse_to_world(mouse_x, mouse_y, viewport);
        if (!remote_mode) {
            if ((mouse_buttons & SDL_BUTTON_LMASK) != 0U) {
                simulation.world().paint_disc(mouse_cell, brush_radius, brush);
            } else if ((mouse_buttons & SDL_BUTTON_RMASK) != 0U) {
                simulation.world().paint_disc(
                    mouse_cell,
                    brush_radius,
                    meat2d::MaterialId::Empty);
            } else if ((mouse_buttons & SDL_BUTTON_MMASK) != 0U) {
                seed_organism_brush(
                    simulation.organisms(),
                    mouse_cell,
                    std::max(1, brush_radius / 2),
                    organism_brush);
            }
        }

        if (remote_mode) {
            while (accumulator >= fixed_seconds) {
                last_network_stats = remote.update();
                ++network_steps;
                if (remote.connected()) {
                    const auto network_radius =
                        static_cast<std::uint8_t>(std::min(brush_radius, 8));
                    if ((mouse_buttons & SDL_BUTTON_LMASK) != 0U) {
                        remote.paint(mouse_cell, brush, network_radius);
                    } else if ((mouse_buttons & SDL_BUTTON_RMASK) != 0U) {
                        remote.paint(
                            mouse_cell,
                            meat2d::MaterialId::Empty,
                            network_radius);
                    } else if (network_steps % 15U == 0U) {
                        remote.set_focus(mouse_cell);
                    }
                }
                accumulator = std::max(0.0, accumulator - fixed_seconds);
            }
            single_step = false;
        } else {
            while ((accumulator >= fixed_seconds && !paused) || single_step) {
                last_stats = simulation.step();
                accumulator = std::max(0.0, accumulator - fixed_seconds);
                single_step = false;
                if (paused) {
                    break;
                }
            }
        }

        displayed_world =
            remote_mode && remote.replicated_world() != nullptr
                ? remote.replicated_world()
                : &simulation.world();
        if (displayed_world->width() != texture_width ||
            displayed_world->height() != texture_height) {
            SDL_DestroyTexture(texture);
            texture_width = displayed_world->width();
            texture_height = displayed_world->height();
            texture = create_world_texture(renderer, texture_width, texture_height);
            if (texture == nullptr) {
                std::fprintf(stderr, "Texture recreation failed: %s\n", SDL_GetError());
                running = false;
                continue;
            }
            pixels.assign(
                static_cast<std::size_t>(texture_width) *
                    static_cast<std::size_t>(texture_height) * 4U,
                0U);
        }

        displayed_world->rasterize_rgba(pixels);
        if (!remote_mode) {
            rasterize_organisms(simulation, pixels);
            rasterize_agents(simulation, pixels);
        }
        SDL_UpdateTexture(
            texture,
            nullptr,
            pixels.data(),
            displayed_world->width() * 4);
        SDL_SetRenderDrawColor(renderer, 5, 7, 12, 255);
        SDL_RenderClear(renderer);
        const auto render_viewport = calculate_viewport(renderer, *displayed_world);
        SDL_RenderTexture(
            renderer,
            texture,
            nullptr,
            &render_viewport.destination);
        SDL_RenderPresent(renderer);
        ++rendered_frames;
        if (options.frame_limit != 0 && rendered_frames >= options.frame_limit) {
            running = false;
        }

        if (now - title_update >= std::chrono::milliseconds(250)) {
            title_update = now;
            std::string title;
            if (remote_mode) {
                const auto snapshot = remote.latest_snapshot();
                const auto connection =
                    remote.connected() ? "ONLINE client " +
                                             std::to_string(remote.client_id())
                                       : "CONNECTING";
                title =
                    "Meat2D Living Lab | " + connection + " | " +
                    std::string(material_name(brush)) +
                    " r=" + std::to_string(std::min(brush_radius, 8)) +
                    " | server tick " +
                    std::to_string(snapshot ? snapshot->server_tick : 0U) +
                    " | agents " +
                    std::to_string(snapshot ? snapshot->agent_count : 0U) +
                    " | organisms " +
                    std::to_string(
                        snapshot ? snapshot->organism_population : 0U) +
                    " | chunks " +
                    std::to_string(last_network_stats.completed_chunks);
            } else {
                title =
                    "Meat2D Living Lab | " + std::string(material_name(brush)) +
                    " | microbe " + organism_name(organism_brush) +
                    " r=" + std::to_string(brush_radius) +
                    (paused ? " | PAUSED" : "") +
                    " | tick " +
                    std::to_string(simulation.world().current_tick()) +
                    " | agents " + std::to_string(simulation.agents().size()) +
                    " | organisms " +
                    std::to_string(simulation.organisms().population()) +
                    " | moved " +
                    std::to_string(last_stats.world.moved_cells) +
                    " | reacted " +
                    std::to_string(last_stats.world.reacted_cells) +
                    " | commands " +
                    std::to_string(last_stats.applied_commands) +
                    " | active chunks " +
                    std::to_string(last_stats.world.active_chunks);
            }
            SDL_SetWindowTitle(window, title.c_str());
        }

        SDL_Delay(1);
    }

    if (remote_mode) {
        remote.disconnect();
    }
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
