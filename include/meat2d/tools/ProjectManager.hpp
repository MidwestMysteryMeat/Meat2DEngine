#pragma once

#include <filesystem>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>

namespace meat2d::tools {

[[nodiscard]] std::filesystem::path
locate_template_root(const std::filesystem::path& executable_path = {});
[[nodiscard]] std::string locate_cmake_executable();
[[nodiscard]] std::string locate_ninja_executable(std::string_view cmake_executable = {});

enum class ProjectTemplate { SideScroller, TopDown, Metroidvania, FallingSand };

enum class BuildProfile { Debug, Release };

enum class RepositoryVisibility { Public, Private };

struct ToolResult {
    bool success{};
    std::string summary;
    std::string details;
};

struct NewProjectOptions {
    std::string name;
    std::filesystem::path directory;
    ProjectTemplate project_template{ProjectTemplate::SideScroller};
    std::string engine_git_tag{"main"};
};

struct PublishOptions {
    std::filesystem::path project_directory;
    std::string repository;
    std::string description;
    RepositoryVisibility visibility{RepositoryVisibility::Public};
};

class ProjectManager {
  public:
    explicit ProjectManager(std::filesystem::path template_root, std::string cmake_executable = {});

    [[nodiscard]] const std::filesystem::path& template_root() const noexcept;
    [[nodiscard]] bool templates_available() const noexcept;

    ToolResult create_project(const NewProjectOptions& options) const;
    ToolResult configure_project(const std::filesystem::path& project_directory,
                                 BuildProfile profile) const;
    ToolResult build_project(const std::filesystem::path& project_directory,
                             BuildProfile profile) const;
    ToolResult package_project(const std::filesystem::path& project_directory) const;
    ToolResult run_project(const std::filesystem::path& project_directory) const;
    ToolResult publish_project(const PublishOptions& options) const;

    [[nodiscard]] static std::string project_slug(std::string_view name);
    [[nodiscard]] static std::string project_identifier(std::string_view name);

  private:
    ToolResult copy_template_tree(const std::filesystem::path& source,
                                  const std::filesystem::path& destination,
                                  const NewProjectOptions& options, bool overwrite) const;
    ToolResult run_cmake(const std::filesystem::path& project_directory,
                         std::initializer_list<std::string> arguments) const;
    ToolResult run_cmake(const std::filesystem::path& project_directory,
                         std::span<const std::string> arguments) const;

    std::filesystem::path template_root_;
    std::filesystem::path engine_source_;
    std::string cmake_executable_;
    std::string ninja_executable_;
};

} // namespace meat2d::tools
