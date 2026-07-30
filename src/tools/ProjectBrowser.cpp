#include "meat2d/tools/ProjectBrowser.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <system_error>

namespace meat2d::tools {
namespace {

std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return text;
}

bool contains_parent_component(const std::filesystem::path& path) {
    return std::any_of(path.begin(), path.end(),
                       [](const std::filesystem::path& component) { return component == ".."; });
}

bool editable_kind(ProjectFileKind kind) {
    switch (kind) {
    case ProjectFileKind::Source:
    case ProjectFileKind::Header:
    case ProjectFileKind::Script:
    case ProjectFileKind::Configuration:
    case ProjectFileKind::Documentation:
        return true;
    case ProjectFileKind::Directory:
    case ProjectFileKind::Image:
    case ProjectFileKind::Audio:
    case ProjectFileKind::Font:
    case ProjectFileKind::Other:
        return false;
    }
    return false;
}

} // namespace

bool ProjectBrowser::open(std::filesystem::path root) {
    close();
    std::error_code error;
    root = std::filesystem::weakly_canonical(root, error);
    if (error || !std::filesystem::is_directory(root, error)) {
        last_error_ = "project root is not a readable directory";
        return false;
    }
    if (!std::filesystem::is_regular_file(root / "CMakeLists.txt", error)) {
        last_error_ = "project root does not contain CMakeLists.txt";
        return false;
    }
    root_ = std::move(root);
    return refresh();
}

bool ProjectBrowser::refresh() {
    entries_.clear();
    if (root_.empty()) {
        last_error_ = "no project is open";
        return false;
    }

    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(
        root_, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::recursive_directory_iterator end;
    while (iterator != end && !error && entries_.size() < maximum_project_entries) {
        const auto path = iterator->path();
        const auto status = iterator->symlink_status(error);
        if (error) {
            break;
        }
        if (std::filesystem::is_symlink(status)) {
            if (std::filesystem::is_directory(status)) {
                iterator.disable_recursion_pending();
            }
            iterator.increment(error);
            continue;
        }

        const bool directory = std::filesystem::is_directory(status);
        if (directory && should_skip_directory(path.filename().string())) {
            iterator.disable_recursion_pending();
            iterator.increment(error);
            continue;
        }
        const auto relative = std::filesystem::relative(path, root_, error);
        if (error) {
            break;
        }
        const auto kind = directory ? ProjectFileKind::Directory : classify(path);
        std::uintmax_t size = 0;
        if (!directory) {
            size = std::filesystem::file_size(path, error);
            if (error) {
                error.clear();
                size = 0;
            }
        }
        auto last_write_time = std::filesystem::last_write_time(path, error);
        if (error) {
            error.clear();
            last_write_time = {};
        }
        entries_.push_back({
            .relative_path = relative,
            .kind = kind,
            .size = size,
            .last_write_time = last_write_time,
            .depth = static_cast<std::size_t>(
                std::max<std::ptrdiff_t>(0, std::distance(relative.begin(), relative.end()) - 1)),
            .editable = !directory && editable_kind(kind) && size <= maximum_editable_file_bytes,
        });
        iterator.increment(error);
    }
    if (error) {
        last_error_ = "project scan failed: " + error.message();
        return false;
    }
    if (entries_.size() >= maximum_project_entries) {
        last_error_ = "project browser stopped at its 10,000-entry safety limit";
    } else {
        last_error_.clear();
    }
    std::sort(entries_.begin(), entries_.end(),
              [](const ProjectEntry& left, const ProjectEntry& right) {
                  return lowercase(left.relative_path.generic_string()) <
                         lowercase(right.relative_path.generic_string());
              });
    return true;
}

void ProjectBrowser::close() noexcept {
    root_.clear();
    entries_.clear();
}

void ProjectBrowser::set_show_generated(bool show) noexcept {
    show_generated_ = show;
}

bool ProjectBrowser::is_open() const noexcept {
    return !root_.empty();
}

bool ProjectBrowser::show_generated() const noexcept {
    return show_generated_;
}

const std::filesystem::path& ProjectBrowser::root() const noexcept {
    return root_;
}

std::span<const ProjectEntry> ProjectBrowser::entries() const noexcept {
    return entries_;
}

std::string_view ProjectBrowser::last_error() const noexcept {
    return last_error_;
}

TextFileResult ProjectBrowser::load_text(const std::filesystem::path& relative_path) const {
    std::error_code error;
    const auto path = resolve_existing(relative_path, error);
    if (error || path.empty() || !std::filesystem::is_regular_file(path, error)) {
        return {
            .success = false,
            .text = {},
            .error = "file is outside the project or is not readable",
        };
    }
    const auto size = std::filesystem::file_size(path, error);
    const auto kind = classify(path);
    if (error || !editable_kind(kind) || size > maximum_editable_file_bytes) {
        return {
            .success = false,
            .text = {},
            .error = "file type or size is not supported by the text editor",
        };
    }
    std::ifstream input(path, std::ios::binary);
    std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if ((!input.good() && !input.eof()) || text.find('\0') != std::string::npos) {
        return {
            .success = false,
            .text = {},
            .error = "file is binary or could not be read",
        };
    }
    return {
        .success = true,
        .text = std::move(text),
        .error = {},
    };
}

BrowserResult ProjectBrowser::save_text(const std::filesystem::path& relative_path,
                                        std::string_view text) {
    if (text.size() > maximum_editable_file_bytes) {
        return {
            .success = false,
            .path = {},
            .message = "editor buffer exceeds the 2 MiB safety limit",
        };
    }
    std::error_code error;
    const auto path = resolve_existing(relative_path, error);
    if (error || path.empty() || !std::filesystem::is_regular_file(path, error) ||
        !editable_kind(classify(path))) {
        return {
            .success = false,
            .path = {},
            .message = "save target is outside the project or not editable",
        };
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) {
        return {
            .success = false,
            .path = {},
            .message = "could not write the selected project file",
        };
    }
    output.close();
    refresh();
    return {
        .success = true,
        .path = path,
        .message = "file saved",
    };
}

BrowserResult ProjectBrowser::create_text_file(const std::filesystem::path& relative_path,
                                               std::string_view initial_text) {
    if (initial_text.size() > maximum_editable_file_bytes) {
        return {
            .success = false,
            .path = {},
            .message = "initial text exceeds the 2 MiB safety limit",
        };
    }
    std::error_code error;
    const auto path = resolve_new(relative_path, error);
    if (error || path.empty() || !editable_kind(classify(path)) ||
        std::filesystem::exists(path, error)) {
        return {
            .success = false,
            .path = {},
            .message = "new file path is unsafe, already exists, or is not editable",
        };
    }
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return {
            .success = false,
            .path = {},
            .message = "could not create the file's parent directory",
        };
    }
    std::ofstream output(path, std::ios::binary);
    output.write(initial_text.data(), static_cast<std::streamsize>(initial_text.size()));
    if (!output) {
        return {
            .success = false,
            .path = {},
            .message = "could not create the project file",
        };
    }
    output.close();
    refresh();
    return {
        .success = true,
        .path = path,
        .message = "file created",
    };
}

BrowserResult ProjectBrowser::import_asset(const std::filesystem::path& source,
                                           const std::filesystem::path& asset_subdirectory) {
    std::error_code error;
    const auto canonical_source = std::filesystem::canonical(source, error);
    if (error || !std::filesystem::is_regular_file(canonical_source, error)) {
        return {
            .success = false,
            .path = {},
            .message = "asset source is not a readable regular file",
        };
    }
    auto destination_directory =
        resolve_new(std::filesystem::path("assets") / asset_subdirectory, error);
    if (error || destination_directory.empty()) {
        return {
            .success = false,
            .path = {},
            .message = "asset destination is outside the project",
        };
    }
    std::filesystem::create_directories(destination_directory, error);
    if (error) {
        return {
            .success = false,
            .path = {},
            .message = "could not create the asset directory",
        };
    }
    auto destination = destination_directory / canonical_source.filename();
    for (std::uint32_t suffix = 2; std::filesystem::exists(destination, error) && suffix < 10'000U;
         ++suffix) {
        destination = destination_directory /
                      (canonical_source.stem().string() + "-" + std::to_string(suffix) +
                       canonical_source.extension().string());
    }
    if (error || std::filesystem::exists(destination, error)) {
        return {
            .success = false,
            .path = {},
            .message = "could not choose a unique asset filename",
        };
    }
    if (!std::filesystem::copy_file(canonical_source, destination,
                                    std::filesystem::copy_options::none, error)) {
        return {
            .success = false,
            .path = {},
            .message = "asset copy failed: " + error.message(),
        };
    }
    refresh();
    return {
        .success = true,
        .path = destination,
        .message = "asset imported",
    };
}

BrowserResult
ProjectBrowser::resolve_for_external_open(const std::filesystem::path& relative_path) const {
    std::error_code error;
    const auto path = resolve_existing(relative_path, error);
    if (error || path.empty()) {
        return {
            .success = false,
            .path = {},
            .message = "path is outside the project or does not exist",
        };
    }
    return {
        .success = true,
        .path = path,
        .message = "path resolved",
    };
}

ProjectFileKind ProjectBrowser::classify(const std::filesystem::path& path) noexcept {
    const auto extension = lowercase(path.extension().string());
    const auto filename = lowercase(path.filename().string());
    if (extension == ".cpp" || extension == ".cc" || extension == ".cxx" || extension == ".c") {
        return ProjectFileKind::Source;
    }
    if (extension == ".hpp" || extension == ".hh" || extension == ".hxx" || extension == ".h" ||
        extension == ".inl" || extension == ".ixx") {
        return ProjectFileKind::Header;
    }
    if (extension == ".lua" || extension == ".py" || extension == ".js" || extension == ".ts" ||
        extension == ".glsl" || extension == ".vert" || extension == ".frag" ||
        extension == ".comp" || extension == ".wgsl") {
        return ProjectFileKind::Script;
    }
    if (extension == ".toml" || extension == ".json" || extension == ".yaml" ||
        extension == ".yml" || extension == ".ini" || extension == ".cfg" ||
        extension == ".cmake" || filename == "cmakelists.txt" || filename == ".gitignore" ||
        filename == ".editorconfig") {
        return ProjectFileKind::Configuration;
    }
    if (extension == ".md" || extension == ".txt" || extension == ".rst") {
        return ProjectFileKind::Documentation;
    }
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" ||
        extension == ".gif" || extension == ".webp" || extension == ".tga") {
        return ProjectFileKind::Image;
    }
    if (extension == ".wav" || extension == ".ogg" || extension == ".mp3" || extension == ".flac") {
        return ProjectFileKind::Audio;
    }
    if (extension == ".ttf" || extension == ".otf" || extension == ".woff" ||
        extension == ".woff2") {
        return ProjectFileKind::Font;
    }
    return ProjectFileKind::Other;
}

bool ProjectBrowser::is_asset(ProjectFileKind kind) noexcept {
    return kind == ProjectFileKind::Image || kind == ProjectFileKind::Audio ||
           kind == ProjectFileKind::Font || kind == ProjectFileKind::Other;
}

bool ProjectBrowser::is_code(ProjectFileKind kind) noexcept {
    return editable_kind(kind);
}

std::string_view ProjectBrowser::kind_name(ProjectFileKind kind) noexcept {
    switch (kind) {
    case ProjectFileKind::Directory:
        return "Folder";
    case ProjectFileKind::Source:
        return "Source";
    case ProjectFileKind::Header:
        return "Header";
    case ProjectFileKind::Script:
        return "Script";
    case ProjectFileKind::Configuration:
        return "Config";
    case ProjectFileKind::Documentation:
        return "Docs";
    case ProjectFileKind::Image:
        return "Image";
    case ProjectFileKind::Audio:
        return "Audio";
    case ProjectFileKind::Font:
        return "Font";
    case ProjectFileKind::Other:
        return "Asset";
    }
    return "Unknown";
}

std::filesystem::path ProjectBrowser::resolve_existing(const std::filesystem::path& relative_path,
                                                       std::error_code& error) const {
    if (root_.empty() || relative_path.empty() || relative_path.is_absolute() ||
        contains_parent_component(relative_path)) {
        error = std::make_error_code(std::errc::permission_denied);
        return {};
    }
    const auto result = std::filesystem::canonical(root_ / relative_path, error);
    if (error || !inside_root(result)) {
        error = std::make_error_code(std::errc::permission_denied);
        return {};
    }
    return result;
}

std::filesystem::path ProjectBrowser::resolve_new(const std::filesystem::path& relative_path,
                                                  std::error_code& error) const {
    if (root_.empty() || relative_path.empty() || relative_path.is_absolute() ||
        contains_parent_component(relative_path)) {
        error = std::make_error_code(std::errc::permission_denied);
        return {};
    }
    const auto result = std::filesystem::weakly_canonical(root_ / relative_path, error);
    if (error || !inside_root(result)) {
        error = std::make_error_code(std::errc::permission_denied);
        return {};
    }
    return result;
}

bool ProjectBrowser::inside_root(const std::filesystem::path& absolute) const {
    std::error_code error;
    const auto relative = std::filesystem::relative(absolute, root_, error);
    return !error && !relative.empty() && !relative.is_absolute() &&
           !contains_parent_component(relative);
}

bool ProjectBrowser::should_skip_directory(std::string_view name) const noexcept {
    if (show_generated_) {
        return name == ".git";
    }
    return name == ".git" || name == ".vs" || name == ".idea" || name == ".cache" ||
           name == "build" || name == "out" || name == "dist" || name == "packages" ||
           name.starts_with("cmake-build-");
}

} // namespace meat2d::tools
