#include "meat2d/tools/ProjectManager.hpp"

#include "meat2d/tools/Process.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>
#include <vector>

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

bool valid_repository(std::string_view repository) {
    const auto slash = repository.find('/');
    return repository.size() <= 200U && slash != std::string_view::npos && slash != 0U &&
           slash + 1U < repository.size() &&
           repository.find('/', slash + 1U) == std::string_view::npos &&
           std::all_of(repository.begin(), repository.end(), [](unsigned char character) {
               return std::isalnum(character) != 0 || character == '.' || character == '-' ||
                      character == '_' || character == '/';
           });
}

bool remote_matches_repository(std::string remote, std::string_view repository) {
    std::transform(remote.begin(), remote.end(), remote.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    std::string expected(repository);
    std::transform(expected.begin(), expected.end(), expected.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    while (!remote.empty() &&
           (remote.back() == '\n' || remote.back() == '\r' || remote.back() == '/')) {
        remote.pop_back();
    }
    if (remote.ends_with(".git")) {
        remote.resize(remote.size() - 4U);
    }
    constexpr std::string_view https_prefix{"https://github.com/"};
    constexpr std::string_view ssh_prefix{"git@github.com:"};
    constexpr std::string_view ssh_url_prefix{"ssh://git@github.com/"};
    if (remote.starts_with(https_prefix)) {
        return remote.substr(https_prefix.size()) == expected;
    }
    if (remote.starts_with(ssh_prefix)) {
        return remote.substr(ssh_prefix.size()) == expected;
    }
    return remote.starts_with(ssh_url_prefix) && remote.substr(ssh_url_prefix.size()) == expected;
}

std::string profile_name(BuildProfile profile) {
    return profile == BuildProfile::Debug ? "dev" : "release";
}

std::string_view template_directory(ProjectTemplate project_template) {
    switch (project_template) {
    case ProjectTemplate::SideScroller:
        return "side_scroller";
    case ProjectTemplate::TopDown:
        return "top_down";
    case ProjectTemplate::TopDownRts:
        return "top_down_rts";
    case ProjectTemplate::Metroidvania:
        return "metroidvania";
    case ProjectTemplate::Castlevania:
        return "castlevania";
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

ToolResult from_process(std::string summary, const ProcessResult& process) {
    return {
        .success = process.success(),
        .summary = process.success() ? std::move(summary) : std::move(summary) + " failed",
        .details = process.output,
    };
}

std::optional<std::string> environment_value(const char* name) {
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
        std::free(value);
        return std::nullopt;
    }
    std::string result(value);
    std::free(value);
    if (result.empty()) {
        return std::nullopt;
    }
    return result;
#else
    const auto* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return std::nullopt;
    }
    return std::string(value);
#endif
}

} // namespace

std::filesystem::path locate_template_root(const std::filesystem::path& executable_path) {
    std::vector<std::filesystem::path> candidates;
    if (const auto environment = environment_value("MEAT2D_TEMPLATE_ROOT")) {
        candidates.emplace_back(*environment);
    }
    if (!executable_path.empty()) {
        std::error_code error;
        const auto absolute = std::filesystem::absolute(executable_path, error);
        if (!error) {
            const auto directory = absolute.parent_path();
            candidates.push_back(directory / "templates");
            candidates.push_back(directory / ".." / "share" / "Meat2D" / "templates");
        }
    }
    candidates.push_back(std::filesystem::current_path() / "templates");
#if defined(MEAT2D_SOURCE_TEMPLATE_ROOT)
    candidates.emplace_back(MEAT2D_SOURCE_TEMPLATE_ROOT);
#endif

    for (auto candidate : candidates) {
        std::error_code error;
        candidate = std::filesystem::weakly_canonical(candidate, error);
        if (!error && std::filesystem::is_directory(candidate / "common", error) &&
            std::filesystem::is_directory(candidate / "side_scroller", error) &&
            std::filesystem::is_directory(candidate / "top_down", error) &&
            std::filesystem::is_directory(candidate / "top_down_rts", error) &&
            std::filesystem::is_directory(candidate / "metroidvania", error) &&
            std::filesystem::is_directory(candidate / "castlevania", error) &&
            std::filesystem::is_directory(candidate / "visual_novel", error) &&
            std::filesystem::is_directory(candidate / "rpg", error) &&
            std::filesystem::is_directory(candidate / "destructible_artillery", error) &&
            std::filesystem::is_directory(candidate / "cellular_roguelite", error) &&
            std::filesystem::is_directory(candidate / "sandbox_survival", error) &&
            std::filesystem::is_directory(candidate / "falling_sand", error)) {
            return candidate;
        }
    }
    return {};
}

std::string locate_cmake_executable() {
    if (const auto environment = environment_value("MEAT2D_CMAKE")) {
        std::error_code error;
        if (std::filesystem::is_regular_file(*environment, error)) {
            return *environment;
        }
    }
    if (run_process({"cmake", "--version"}).success()) {
        return "cmake";
    }

    std::vector<std::filesystem::path> candidates;
#if defined(_WIN32)
    if (const auto program_files = environment_value("ProgramFiles")) {
        const std::filesystem::path root(*program_files);
        candidates.push_back(root / "CMake" / "bin" / "cmake.exe");
        for (const auto edition : {"Community", "Professional", "Enterprise", "BuildTools"}) {
            candidates.push_back(root / "Microsoft Visual Studio" / "2022" / edition / "Common7" /
                                 "IDE" / "CommonExtensions" / "Microsoft" / "CMake" / "CMake" /
                                 "bin" / "cmake.exe");
        }
    }
    if (const auto profile = environment_value("USERPROFILE")) {
        candidates.push_back(std::filesystem::path(*profile) / "scoop" / "apps" / "cmake" /
                             "current" / "bin" / "cmake.exe");
    }
#else
    candidates = {
        "/usr/bin/cmake",
        "/usr/local/bin/cmake",
        "/opt/homebrew/bin/cmake",
    };
#endif
    for (const auto& candidate : candidates) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error)) {
            return candidate.string();
        }
    }
    return "cmake";
}

std::string locate_ninja_executable(std::string_view cmake_executable) {
    if (const auto environment = environment_value("MEAT2D_NINJA")) {
        std::error_code error;
        if (std::filesystem::is_regular_file(*environment, error)) {
            return *environment;
        }
    }
    if (run_process({"ninja", "--version"}).success()) {
        return "ninja";
    }

    std::vector<std::filesystem::path> candidates;
    if (!cmake_executable.empty() && cmake_executable != "cmake") {
        const std::filesystem::path cmake_path(cmake_executable);
        candidates.push_back(cmake_path.parent_path().parent_path().parent_path() / "Ninja" /
                             "ninja.exe");
    }
#if defined(_WIN32)
    if (const auto profile = environment_value("USERPROFILE")) {
        candidates.push_back(std::filesystem::path(*profile) / "scoop" / "apps" / "ninja" /
                             "current" / "ninja.exe");
    }
#else
    candidates.push_back("/usr/bin/ninja");
    candidates.push_back("/usr/local/bin/ninja");
    candidates.push_back("/opt/homebrew/bin/ninja");
#endif
    for (const auto& candidate : candidates) {
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error)) {
            return candidate.string();
        }
    }
    return {};
}

ProjectManager::ProjectManager(std::filesystem::path template_root, std::string cmake_executable)
    : template_root_(std::move(template_root)), engine_source_(),
      cmake_executable_(cmake_executable.empty() ? locate_cmake_executable()
                                                 : std::move(cmake_executable)),
      ninja_executable_(locate_ninja_executable(cmake_executable_)) {
    std::error_code error;
    const auto candidate = template_root_.parent_path();
    if (std::filesystem::is_regular_file(candidate / "CMakeLists.txt", error)) {
        engine_source_ = candidate;
    }
}

const std::filesystem::path& ProjectManager::template_root() const noexcept {
    return template_root_;
}

bool ProjectManager::templates_available() const noexcept {
    std::error_code error;
    return std::filesystem::is_directory(template_root_ / "common", error) &&
           std::filesystem::is_directory(template_root_ / "side_scroller", error) &&
           std::filesystem::is_directory(template_root_ / "top_down", error) &&
           std::filesystem::is_directory(template_root_ / "top_down_rts", error) &&
           std::filesystem::is_directory(template_root_ / "metroidvania", error) &&
           std::filesystem::is_directory(template_root_ / "castlevania", error) &&
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
            .details = "Choose side, top, rts, metroidvania, castlevania, visual-novel, rpg, or falling-sand.",
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

ToolResult ProjectManager::configure_project(const std::filesystem::path& project_directory,
                                             BuildProfile profile) const {
    std::vector<std::string> arguments{
        "--preset",
        profile_name(profile),
    };
    if (!engine_source_.empty()) {
        arguments.push_back("-DMEAT2D_ENGINE_SOURCE=" + engine_source_.string());
    }
    if (!ninja_executable_.empty()) {
        arguments.push_back("-DCMAKE_MAKE_PROGRAM=" + ninja_executable_);
    }
    return run_cmake(project_directory, arguments);
}

ToolResult ProjectManager::build_project(const std::filesystem::path& project_directory,
                                         BuildProfile profile) const {
    auto configured = configure_project(project_directory, profile);
    if (!configured.success) {
        return configured;
    }
    auto built = run_cmake(project_directory, {"--build", "--preset", profile_name(profile)});
    built.details = configured.details + built.details;
    return built;
}

ToolResult ProjectManager::package_project(const std::filesystem::path& project_directory) const {
    auto built = build_project(project_directory, BuildProfile::Release);
    if (!built.success) {
        return built;
    }
    auto packaged = run_cmake(project_directory,
                              {"--build", "build", "--config", "Release", "--target", "package"});
    packaged.details = built.details + packaged.details;
    if (packaged.success) {
        packaged.summary = "Release packages created in build";
    }
    return packaged;
}

ToolResult ProjectManager::run_project(const std::filesystem::path& project_directory) const {
    auto built = build_project(project_directory, BuildProfile::Debug);
    if (!built.success) {
        return built;
    }

    std::ifstream manifest(project_directory / "game.toml");
    std::string line;
    std::string slug;
    while (std::getline(manifest, line)) {
        const auto equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        auto key = std::string_view(line).substr(0, equals);
        while (!key.empty() && std::isspace(static_cast<unsigned char>(key.front())) != 0) {
            key.remove_prefix(1);
        }
        while (!key.empty() && std::isspace(static_cast<unsigned char>(key.back())) != 0) {
            key.remove_suffix(1);
        }
        if (key != "slug") {
            continue;
        }
        const auto first_quote = line.find('"', equals);
        const auto second_quote =
            first_quote == std::string::npos ? std::string::npos : line.find('"', first_quote + 1U);
        if (first_quote != std::string::npos && second_quote != std::string::npos) {
            slug = line.substr(first_quote + 1U, second_quote - first_quote - 1U);
            break;
        }
    }
    if (slug.empty() || !std::all_of(slug.begin(), slug.end(), [](unsigned char character) {
            return std::isalnum(character) != 0 || character == '-' || character == '_';
        })) {
        return {
            .summary = "Could not identify the game executable",
            .details = "game.toml must contain a simple quoted slug.",
        };
    }

#if defined(_WIN32)
    slug += ".exe";
#endif
    const std::vector<std::filesystem::path> candidates{
        project_directory / "build" / "Debug" / slug,
        project_directory / "build" / slug,
        project_directory / "build" / "dev" / slug,
        project_directory / "build" / "dev" / "Debug" / slug,
    };
    for (const auto& executable : candidates) {
        if (!std::filesystem::is_regular_file(executable)) {
            continue;
        }
        const auto process = run_process({executable.string()}, project_directory);
        auto result = from_process("Game test completed", process);
        result.details = built.details + result.details;
        return result;
    }
    return {
        .summary = "Game built but its executable was not found",
        .details = built.details,
    };
}

ToolResult ProjectManager::publish_project(const PublishOptions& options) const {
    if (!valid_repository(options.repository) ||
        !std::filesystem::is_regular_file(options.project_directory / "CMakeLists.txt")) {
        return {
            .summary = "Publish settings are invalid",
            .details = "Use OWNER/REPOSITORY and select a generated project.",
        };
    }

    const auto& directory = options.project_directory;
    std::string log;
    if (!std::filesystem::is_directory(directory / ".git")) {
        auto initialized = run_process({"git", "init", "-b", "main"}, directory);
        log += initialized.output;
        if (!initialized.success()) {
            return from_process("Git initialization", initialized);
        }
    }

    auto added = run_process({"git", "add", "--all"}, directory);
    log += added.output;
    if (!added.success()) {
        return from_process("Git staging", added);
    }
    const auto status = run_process({"git", "status", "--porcelain"}, directory);
    log += status.output;
    if (!status.success()) {
        return from_process("Git status", status);
    }
    if (!status.output.empty()) {
        auto committed = run_process({"git", "commit", "-m", "Initial Meat2D game"}, directory);
        log += committed.output;
        if (!committed.success()) {
            return {
                .summary = "Git commit failed",
                .details = log,
            };
        }
    }

    const auto existing_remote = run_process({"git", "remote", "get-url", "origin"}, directory);
    if (existing_remote.success()) {
        if (!remote_matches_repository(existing_remote.output, options.repository)) {
            return {
                .summary = "Origin does not match the requested repository",
                .details = "Existing origin: " + existing_remote.output +
                           "Requested: " + options.repository + "\n",
            };
        }
        auto pushed = run_process({"git", "push", "-u", "origin", "HEAD"}, directory);
        pushed.output = log + pushed.output;
        return from_process("Existing repository pushed", pushed);
    }

    std::vector<std::string> command{
        "gh",
        "repo",
        "create",
        options.repository,
        options.visibility == RepositoryVisibility::Public ? "--public" : "--private",
        "--source",
        ".",
        "--remote",
        "origin",
        "--push",
    };
    if (!options.description.empty()) {
        command.push_back("--description");
        command.push_back(options.description);
    }
    auto published = run_process(command, directory);
    published.output = log + published.output;
    return from_process("GitHub repository created and pushed", published);
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

ToolResult ProjectManager::run_cmake(const std::filesystem::path& project_directory,
                                     std::initializer_list<std::string> arguments) const {
    return run_cmake(project_directory,
                     std::span<const std::string>(arguments.begin(), arguments.size()));
}

ToolResult ProjectManager::run_cmake(const std::filesystem::path& project_directory,
                                     std::span<const std::string> arguments) const {
    if (!std::filesystem::is_regular_file(project_directory / "CMakeLists.txt")) {
        return {
            .summary = "Project directory is invalid",
            .details = project_directory.string(),
        };
    }
    std::vector<std::string> command{cmake_executable_};
    command.insert(command.end(), arguments.begin(), arguments.end());
    const auto process = run_process(command, project_directory);
    return from_process("CMake command completed", process);
}

} // namespace meat2d::tools
