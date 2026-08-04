#include "meat2d/tools/ProjectManager.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace meat2d::tools {
namespace {

void replace_all(std::string& text, std::string_view token, std::string_view replacement) {
    std::size_t position = 0;
    while ((position = text.find(token, position)) != std::string::npos) {
        text.replace(position, token.size(), replacement);
        position += replacement.size();
    }
}

bool valid_git_tag(std::string_view tag) {
    return !tag.empty() && tag.size() <= 128U &&
           std::all_of(tag.begin(), tag.end(), [](unsigned char character) {
               return std::isalnum(character) != 0 || character == '.' || character == '-' ||
                      character == '_' || character == '/';
           });
}

bool valid_project_name(std::string_view name) {
    if (name.empty() || name.size() > 80U || name.front() == ' ' || name.back() == ' ') {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](unsigned char character) {
        return std::isalnum(character) != 0 || character == ' ' || character == '-' ||
               character == '_' || character == '.' || character == '\'' || character == '&' ||
               character == '(' || character == ')' || character == ',';
    });
}

std::string_view template_directory(ProjectTemplate project_template) {
    switch (project_template) {
    case ProjectTemplate::SideScroller:
        return "side_scroller";
    case ProjectTemplate::TopDown:
        return "top_down";
    case ProjectTemplate::Metroidvania:
        return "metroidvania";
    case ProjectTemplate::VisualNovel:
        return "visual_novel";
    case ProjectTemplate::Rpg:
        return "rpg";
    case ProjectTemplate::DestructibleArtillery:
        return "destructible_artillery";
    case ProjectTemplate::CellularRoguelite:
        return "cellular_roguelite";
    case ProjectTemplate::SandboxSurvival:
        return "sandbox_survival";
    case ProjectTemplate::FallingSand:
        return "falling_sand";
    }
    return {};
}

} // namespace

bool ProjectManager::templates_available() const noexcept {
    std::error_code error;
    return std::filesystem::is_directory(template_root_ / "common", error) &&
           std::filesystem::is_directory(template_root_ / "side_scroller", error) &&
           std::filesystem::is_directory(template_root_ / "top_down", error) &&
           std::filesystem::is_directory(template_root_ / "metroidvania", error) &&
           std::filesystem::is_directory(template_root_ / "visual_novel", error) &&
           std::filesystem::is_directory(template_root_ / "rpg", error) &&
           std::filesystem::is_directory(template_root_ / "destructible_artillery", error) &&
           std::filesystem::is_directory(template_root_ / "cellular_roguelite", error) &&
           std::filesystem::is_directory(template_root_ / "sandbox_survival", error) &&
           std::filesystem::is_directory(template_root_ / "falling_sand", error);
}

ToolResult ProjectManager::create_project(const NewProjectOptions& options) const {
    const auto slug = project_slug(options.name);
    if (!valid_project_name(options.name) || slug.empty() || options.directory.empty() ||
        !valid_git_tag(options.engine_git_tag)) {
        return {
            .summary = "Project settings are invalid",
            .details = "Use an 80-character letters/numbers project name, a "
                       "destination, and a simple Git tag or branch.",
        };
    }
    if (!templates_available()) {
        return {
            .summary = "Project templates are unavailable",
            .details = template_root_.string(),
        };
    }

    std::error_code error;
    if (std::filesystem::exists(options.directory, error) &&
        (!std::filesystem::is_directory(options.directory, error) ||
         !std::filesystem::is_empty(options.directory, error))) {
        return {
            .summary = "Destination is not empty",
            .details = options.directory.string(),
        };
    }
    if (!std::filesystem::create_directories(options.directory, error) && error) {
        return {
            .summary = "Could not create destination",
            .details = error.message(),
        };
    }

    auto result = copy_template_tree(template_root_ / "common", options.directory, options, false);
    if (!result.success) {
        return result;
    }
    const auto variant = template_directory(options.project_template);
    if (variant.empty()) {
        return {
            .summary = "Project template is invalid",
            .details = "Choose side, top, metroidvania, visual-novel, rpg, artillery, cellular-roguelite, falling-sand, or sandbox-survival.",
        };
    }
    result = copy_template_tree(template_root_ / std::filesystem::path(variant), options.directory,
                                options, true);
    if (!result.success) {
        return result;
    }
    return {
        .success = true,
        .summary = "Project created",
        .details = options.directory.string(),
    };
}

std::string ProjectManager::project_slug(std::string_view name) {
    std::string result;
    bool pending_dash = false;
    for (const auto raw : name) {
        const auto character = static_cast<unsigned char>(raw);
        if (std::isalnum(character) != 0) {
            if (pending_dash && !result.empty()) {
                result.push_back('-');
            }
            result.push_back(static_cast<char>(std::tolower(character)));
            pending_dash = false;
        } else {
            pending_dash = true;
        }
    }
    return result;
}

std::string ProjectManager::project_identifier(std::string_view name) {
    std::string result;
    bool capitalize = true;
    for (const auto raw : name) {
        const auto character = static_cast<unsigned char>(raw);
        if (std::isalnum(character) == 0) {
            capitalize = true;
            continue;
        }
        result.push_back(capitalize ? static_cast<char>(std::toupper(character))
                                    : static_cast<char>(character));
        capitalize = false;
    }
    if (result.empty() || std::isdigit(static_cast<unsigned char>(result.front())) != 0) {
        result.insert(0, "Game");
    }
    return result;
}

ToolResult ProjectManager::copy_template_tree(const std::filesystem::path& source,
                                              const std::filesystem::path& destination,
                                              const NewProjectOptions& options,
                                              bool overwrite) const {
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator iterator(source, error), end;
         iterator != end && !error; iterator.increment(error)) {
        const auto relative = std::filesystem::relative(iterator->path(), source, error);
        if (error) {
            break;
        }
        const auto target = destination / relative;
        if (iterator->is_directory()) {
            std::filesystem::create_directories(target, error);
            if (error) {
                break;
            }
            continue;
        }
        std::filesystem::create_directories(target.parent_path(), error);
        if (error) {
            break;
        }

        std::ifstream input(iterator->path(), std::ios::binary);
        std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        if (!input.good() && !input.eof()) {
            return {
                .summary = "Could not read template",
                .details = iterator->path().string(),
            };
        }
        replace_all(text, "{{PROJECT_NAME}}", options.name);
        replace_all(text, "{{PROJECT_SLUG}}", project_slug(options.name));
        replace_all(text, "{{PROJECT_IDENTIFIER}}", project_identifier(options.name));
        replace_all(text, "{{ENGINE_GIT_TAG}}", options.engine_git_tag);

        if (!overwrite && std::filesystem::exists(target, error)) {
            return {
                .summary = "Template would overwrite a file",
                .details = target.string(),
            };
        }
        std::ofstream output(target, std::ios::binary | std::ios::trunc);
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!output) {
            return {
                .summary = "Could not write project file",
                .details = target.string(),
            };
        }
    }
    if (error) {
        return {
            .summary = "Template copy failed",
            .details = error.message(),
        };
    }
    return {
        .success = true,
        .summary = "Template copied",
        .details = {},
    };
}

} // namespace meat2d::tools
