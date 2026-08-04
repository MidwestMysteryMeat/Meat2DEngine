#include "meat2d/ai/LivingSimulation.hpp"
#include "meat2d/ai/Crowd.hpp"
#include "meat2d/ai/LearningAgent.hpp"
#include "meat2d/ai/LearningEnvironment.hpp"
#include "meat2d/ai/NeuralNetwork.hpp"
#include "meat2d/assets/Animation.hpp"
#include "meat2d/assets/SpriteSheet.hpp"
#include "meat2d/assets/TileMap.hpp"
#include "meat2d/assets/TextureAtlas.hpp"
#include "meat2d/core/DeterministicRng.hpp"
#include "meat2d/core/FixedTimestep.hpp"
#include "meat2d/input/Input.hpp"
#include "meat2d/net/ChunkCodec.hpp"
#include "meat2d/net/Discovery.hpp"
#include "meat2d/net/Fragmentation.hpp"
#include "meat2d/net/PacketCodec.hpp"
#include "meat2d/net/Protocol.hpp"
#include "meat2d/net/Reliability.hpp"
#include "meat2d/net/Session.hpp"
#include "meat2d/net/UdpSocket.hpp"
#include "meat2d/replay/Replay.hpp"
#include "meat2d/render/Camera.hpp"
#include "meat2d/render/DebugDraw.hpp"
#include "meat2d/render/Particles.hpp"
#include "meat2d/render/SpriteBatch.hpp"
#include "meat2d/render/StaticMeshBatch.hpp"
#include "meat2d/render/WorldView.hpp"
#include "meat2d/scene/Physics.hpp"
#include "meat2d/scene/Scene.hpp"
#include "meat2d/scene/SceneHistory.hpp"
#include "meat2d/scene/SceneSnapshot.hpp"
#include "meat2d/scene/SceneStack.hpp"
#include "meat2d/sim/ChunkStore.hpp"
#include "meat2d/sim/Projectile.hpp"
#include "meat2d/sim/Scenario.hpp"
#include "meat2d/sim/World.hpp"
#include "meat2d/tools/ProjectBrowser.hpp"
#include "meat2d/tools/ProjectManager.hpp"
#include "meat2d/tools/SceneEditor.hpp"
#include "meat2d/tools/McpGateway.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "TestSupport.hpp"

namespace meat2d_tests {

void test_project_browser_safety_and_editing() {
    const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto test_parent = std::filesystem::temp_directory_path() / ("meat2d-browser-" + unique);
    const auto project = test_parent / "project";
    std::error_code error;
    std::filesystem::create_directories(project / "src", error);
    std::filesystem::create_directories(project / "build", error);
    {
        std::ofstream(project / "CMakeLists.txt") << "project(BrowserTest)\n";
        std::ofstream(project / "src" / "main.cpp") << "int main() { return 0; }\n";
        std::ofstream(project / "build" / "generated.cpp") << "generated\n";
        std::ofstream(test_parent / "sample.png", std::ios::binary) << "fake image payload";
    }

    meat2d::tools::ProjectBrowser browser;
    check(browser.open(project), "project browser could not open a valid project");
    const auto generated_hidden =
        std::none_of(browser.entries().begin(), browser.entries().end(),
                     [](const meat2d::tools::ProjectEntry& entry) {
                         return entry.relative_path.generic_string().starts_with("build/");
                     });
    check(generated_hidden, "project browser exposed generated build files by default");
    const auto source_entry =
        std::find_if(browser.entries().begin(), browser.entries().end(),
                     [](const meat2d::tools::ProjectEntry& entry) {
                         return entry.relative_path.generic_string() == "src/main.cpp";
                     });
    check(source_entry != browser.entries().end() &&
              source_entry->last_write_time ==
                  std::filesystem::last_write_time(project / "src" / "main.cpp", error),
          "project browser did not expose the source file modification time");

    auto loaded = browser.load_text("src/main.cpp");
    check(loaded.success, "project browser could not load source text");
    check(loaded.text.find("int main() { return 0; }") != std::string::npos,
          "project browser changed loaded source text");
    const auto saved = browser.save_text("src/main.cpp", "int main() { return 7; }\n");
    check(saved.success, "project browser could not save source text");
    loaded = browser.load_text("src/main.cpp");
    check(loaded.success && loaded.text == "int main() { return 7; }\n",
          "project browser did not persist the edited source");

    const auto created = browser.create_text_file("config/settings.toml", "[game]\nname='test'\n");
    check(created.success, "project browser could not create a config file");
    const auto imported = browser.import_asset(test_parent / "sample.png", "textures");
    check(imported.success, "project browser could not import an asset");
    check(imported.success && imported.path.parent_path().filename() == "textures" &&
              std::filesystem::is_regular_file(imported.path),
          "asset import escaped or missed the requested project folder");

    check(!browser.load_text("../outside.txt").success,
          "project browser allowed parent-directory traversal");
    check(!browser.create_text_file("../escape.cpp", {}).success,
          "project browser created a file outside the project");
    check(!browser.resolve_for_external_open(test_parent / "sample.png").success,
          "external-open resolver accepted an absolute path");

    browser.set_show_generated(true);
    check(browser.refresh(), "project browser could not rescan generated files");
    check(std::any_of(browser.entries().begin(), browser.entries().end(),
                      [](const meat2d::tools::ProjectEntry& entry) {
                          return entry.relative_path.generic_string() == "build/generated.cpp";
                      }),
          "generated-file opt-in did not expose the build tree");

    browser.close();
    std::filesystem::remove_all(test_parent, error);
    check(!error, "project browser test files could not be cleaned up");
}

void test_project_manager_validation_and_templates() {
    const auto templates = meat2d::tools::locate_template_root();
    check(!templates.empty(), "project manager could not locate its templates");
    if (templates.empty()) {
        return;
    }
    meat2d::tools::ProjectManager manager(templates);
    const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto test_parent =
        std::filesystem::temp_directory_path() / ("meat2d-project-manager-" + unique);

    const auto invalid = manager.create_project({
        .name = "Bad \" CMake ${Name}",
        .directory = test_parent / "invalid",
        .project_template = meat2d::tools::ProjectTemplate::SideScroller,
        .engine_git_tag = "main",
    });
    check(!invalid.success && !std::filesystem::exists(test_parent / "invalid"),
          "project manager accepted an injectable project name");

    const auto valid = manager.create_project({
        .name = "Meat & Potatoes (Test)",
        .directory = test_parent / "valid",
        .project_template = meat2d::tools::ProjectTemplate::TopDown,
        .engine_git_tag = "main",
    });
    check(valid.success, "project manager could not create a valid starter");
    check(std::filesystem::is_regular_file(test_parent / "valid" / "src" / "main.cpp") &&
              std::filesystem::is_regular_file(test_parent / "valid" / "CMakePresets.json") &&
              std::filesystem::is_regular_file(test_parent / "valid" / ".github" / "workflows" /
                                               "build.yml"),
          "generated starter omitted code, presets, or publishing workflow");

    struct TemplateCase {
        meat2d::tools::ProjectTemplate project_template;
        std::string_view directory;
    };
    const std::array<TemplateCase, 9> project_templates{
        TemplateCase{meat2d::tools::ProjectTemplate::SideScroller, "side_scroller"},
        TemplateCase{meat2d::tools::ProjectTemplate::TopDown, "top_down"},
        TemplateCase{meat2d::tools::ProjectTemplate::Metroidvania, "metroidvania"},
        TemplateCase{meat2d::tools::ProjectTemplate::VisualNovel, "visual_novel"},
        TemplateCase{meat2d::tools::ProjectTemplate::Rpg, "rpg"},
        TemplateCase{meat2d::tools::ProjectTemplate::DestructibleArtillery,
                     "destructible_artillery"},
        TemplateCase{meat2d::tools::ProjectTemplate::CellularRoguelite, "cellular_roguelite"},
        TemplateCase{meat2d::tools::ProjectTemplate::FallingSand, "falling_sand"},
        TemplateCase{meat2d::tools::ProjectTemplate::SandboxSurvival, "sandbox_survival"},
    };
    for (std::size_t index = 0; index < project_templates.size(); ++index) {
        const auto result = manager.create_project({
            .name = "Template Test " + std::to_string(index),
            .directory = test_parent / ("template-" + std::to_string(index)),
            .project_template = project_templates[index].project_template,
            .engine_git_tag = "main",
        });
        check(result.success, "project manager could not create a selectable game template");
        check(std::filesystem::is_regular_file(test_parent / ("template-" + std::to_string(index)) /
                                               "src" / "main.cpp"),
              "selectable game template omitted its source starter");
        check(std::filesystem::is_regular_file(templates / project_templates[index].directory /
                                               "DESIGN.md"),
              "selectable game template omitted its design contract");
    }

    const std::array<std::string_view, 4> fixed_tick_templates{
        "side_scroller", "top_down", "metroidvania", "falling_sand"};
    for (const auto template_name : fixed_tick_templates) {
        std::ifstream source(templates / template_name / "src" / "main.cpp");
        const std::string contents{std::istreambuf_iterator<char>(source),
                                   std::istreambuf_iterator<char>()};
        check(contents.find("meat2d/core/FixedTimestep.hpp") != std::string::npos,
              "interactive template does not use the shared fixed timestep");
        check(contents.find("fixed_seconds") == std::string::npos &&
                  contents.find("double accumulator") == std::string::npos,
              "interactive template retained a duplicated floating-point accumulator");
    }

    std::error_code error;
    std::filesystem::remove_all(test_parent, error);
    check(!error, "project manager test files could not be cleaned up");
}

void test_sprite_sheet_metadata() {
    const meat2d::assets::SpriteSheet sheet{
        .image = "assets/sprites/player.png",
        .frame_width = 16,
        .frame_height = 16,
        .margin = 1,
        .spacing = 2,
        .animations =
            {
                {
                    .name = "idle",
                    .first_frame = 0,
                    .frame_count = 2,
                    .frames_per_second = 6,
                    .loop = true,
                },
                {
                    .name = "run",
                    .first_frame = 2,
                    .frame_count = 4,
                    .frames_per_second = 12,
                    .loop = true,
                },
            },
    };
    check(meat2d::assets::valid_sprite_sheet(sheet, 70, 36),
          "valid sprite sheet metadata was rejected");
    check(meat2d::assets::sprite_frame_count(sheet, 70, 36) == 6,
          "sprite grid frame count is incorrect");
    const auto frame = meat2d::assets::sprite_frame(sheet, 70, 36, 5);
    check(frame && frame->x == 37 && frame->y == 19 && frame->width == 16 && frame->height == 16,
          "sprite frame rectangle is incorrect");
    check(!meat2d::assets::sprite_frame(sheet, 70, 36, 6).has_value(),
          "out-of-range sprite frame was returned");

    const auto encoded = meat2d::assets::encode_sprite_sheet_toml(sheet);
    check(!encoded.empty(), "sprite sheet metadata did not encode");
    const auto decoded = meat2d::assets::decode_sprite_sheet_toml(encoded);
    check(decoded.sheet && *decoded.sheet == sheet,
          "sprite sheet metadata changed during TOML round trip");

    const auto unsafe = meat2d::assets::decode_sprite_sheet_toml("version = 1\n"
                                                                 "image = \"../outside.png\"\n"
                                                                 "frame_width = 16\n"
                                                                 "frame_height = 16\n");
    check(!unsafe.sheet.has_value(), "sprite metadata accepted a path outside the project");
    const auto malformed = meat2d::assets::decode_sprite_sheet_toml("version = 1\nunknown = 3\n");
    check(!malformed.sheet.has_value() && malformed.error_line == 2,
          "malformed sprite metadata did not report its source line");
    const auto hash_in_path = meat2d::assets::decode_sprite_sheet_toml(
        "version = 1\n"
        "image = \"assets/player#alternate.png\" # inline comment\n"
        "frame_width = 16\n"
        "frame_height = 16\n");
    check(hash_in_path.sheet && hash_in_path.sheet->image == "assets/player#alternate.png",
          "sprite metadata treated a hash inside a string as a comment");
}

void test_texture_atlas_cache() {
    const meat2d::assets::SpriteSheet sheet{
        .image = "assets/player.png",
        .frame_width = 16,
        .frame_height = 16,
        .margin = 0,
        .spacing = 0,
        .animations = {},
    };
    meat2d::assets::TextureAtlasCache first(2);
    meat2d::assets::TextureAtlasCache second(2);
    check(first.define(7, sheet, 32, 16) && second.define(8, sheet, 32, 16) &&
              first.define(8, sheet, 32, 16) && second.define(7, sheet, 32, 16) &&
              first.state_hash() == second.state_hash(),
          "texture atlas cache hash depended on registration order");
    const auto region = first.resolve(7, 1);
    check(region && region->image == sheet.image && region->source == meat2d::RectI{16, 0, 16, 16},
          "texture atlas cache did not resolve a validated frame rectangle");
    const auto before_invalid = first.state_hash();
    meat2d::assets::SpriteSheet unsafe{};
    unsafe.image = "../unsafe.png";
    check(!first.define(9, unsafe, 32, 16) &&
              first.state_hash() == before_invalid && !first.resolve(999, 0).has_value(),
          "texture atlas cache accepted unsafe or unknown content");
    check(first.remove(8) && !first.remove(8) && first.size() == 1U,
          "texture atlas cache remove semantics were not deterministic");
}

} // namespace meat2d_tests
