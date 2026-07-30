#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace meat2d::tools {

inline constexpr std::uintmax_t maximum_editable_file_bytes = 2U * 1024U * 1024U;
inline constexpr std::size_t maximum_project_entries = 10'000;

enum class ProjectFileKind : std::uint8_t {
    Directory,
    Source,
    Header,
    Script,
    Configuration,
    Documentation,
    Image,
    Audio,
    Font,
    Other
};

struct ProjectEntry {
    std::filesystem::path relative_path;
    ProjectFileKind kind{ProjectFileKind::Other};
    std::uintmax_t size{};
    std::size_t depth{};
    bool editable{};
};

struct TextFileResult {
    bool success{};
    std::string text;
    std::string error;
};

struct BrowserResult {
    bool success{};
    std::filesystem::path path;
    std::string message;
};

class ProjectBrowser {
  public:
    bool open(std::filesystem::path root);
    bool refresh();
    void close() noexcept;
    void set_show_generated(bool show) noexcept;

    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] bool show_generated() const noexcept;
    [[nodiscard]] const std::filesystem::path& root() const noexcept;
    [[nodiscard]] std::span<const ProjectEntry> entries() const noexcept;
    [[nodiscard]] std::string_view last_error() const noexcept;

    [[nodiscard]] TextFileResult load_text(const std::filesystem::path& relative_path) const;
    [[nodiscard]] BrowserResult save_text(const std::filesystem::path& relative_path,
                                          std::string_view text);
    [[nodiscard]] BrowserResult create_text_file(const std::filesystem::path& relative_path,
                                                 std::string_view initial_text = {});
    [[nodiscard]] BrowserResult import_asset(const std::filesystem::path& source,
                                             const std::filesystem::path& asset_subdirectory = {});
    [[nodiscard]] BrowserResult
    resolve_for_external_open(const std::filesystem::path& relative_path) const;

    [[nodiscard]] static ProjectFileKind classify(const std::filesystem::path& path) noexcept;
    [[nodiscard]] static bool is_asset(ProjectFileKind kind) noexcept;
    [[nodiscard]] static bool is_code(ProjectFileKind kind) noexcept;
    [[nodiscard]] static std::string_view kind_name(ProjectFileKind kind) noexcept;

  private:
    [[nodiscard]] std::filesystem::path resolve_existing(const std::filesystem::path& relative_path,
                                                         std::error_code& error) const;
    [[nodiscard]] std::filesystem::path resolve_new(const std::filesystem::path& relative_path,
                                                    std::error_code& error) const;
    [[nodiscard]] bool inside_root(const std::filesystem::path& absolute) const;
    [[nodiscard]] bool should_skip_directory(std::string_view name) const noexcept;

    std::filesystem::path root_;
    std::vector<ProjectEntry> entries_;
    std::string last_error_;
    bool show_generated_{};
};

} // namespace meat2d::tools
