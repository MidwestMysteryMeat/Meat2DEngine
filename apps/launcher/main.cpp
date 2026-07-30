#include "meat2d/assets/SpriteSheet.hpp"
#include "meat2d/core/Version.hpp"
#include "meat2d/net/Discovery.hpp"
#include "meat2d/tools/ProjectBrowser.hpp"
#include "meat2d/tools/ProjectManager.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_process.h>
#include <SDL3_image/SDL_image.h>

#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <future>
#include <iterator>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using meat2d::tools::BrowserResult;
using meat2d::tools::BuildProfile;
using meat2d::tools::ProjectBrowser;
using meat2d::tools::ProjectEntry;
using meat2d::tools::ProjectFileKind;
using meat2d::tools::ProjectManager;
using meat2d::tools::ToolResult;

struct DialogInbox {
    std::mutex mutex;
    std::optional<std::string> new_project_parent;
    std::optional<std::string> open_project;
    std::optional<std::string> import_asset;
    bool failed{};
};

DialogInbox dialog_inbox;

void SDLCALL new_project_folder_callback(void*, const char* const* files, int) {
    std::scoped_lock lock(dialog_inbox.mutex);
    if (files == nullptr) {
        dialog_inbox.failed = true;
    } else if (files[0] != nullptr) {
        dialog_inbox.new_project_parent = files[0];
    }
}

void SDLCALL open_project_folder_callback(void*, const char* const* files, int) {
    std::scoped_lock lock(dialog_inbox.mutex);
    if (files == nullptr) {
        dialog_inbox.failed = true;
    } else if (files[0] != nullptr) {
        dialog_inbox.open_project = files[0];
    }
}

void SDLCALL import_asset_callback(void*, const char* const* files, int) {
    std::scoped_lock lock(dialog_inbox.mutex);
    if (files == nullptr) {
        dialog_inbox.failed = true;
    } else if (files[0] != nullptr) {
        dialog_inbox.import_asset = files[0];
    }
}

void poll_dialogs(struct EditorState& state);

std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return text;
}

bool contains_case_insensitive(std::string_view text, std::string_view search) {
    if (search.empty()) {
        return true;
    }
    return lowercase(std::string(text)).find(lowercase(std::string(search))) != std::string::npos;
}

std::string byte_size(std::uintmax_t bytes) {
    if (bytes < 1'024U) {
        return std::to_string(bytes) + " B";
    }
    if (bytes < 1'024U * 1'024U) {
        return std::to_string(bytes / 1'024U) + " KiB";
    }
    return std::to_string(bytes / (1'024U * 1'024U)) + " MiB";
}

std::string file_url(const std::filesystem::path& path) {
    const auto source = path.generic_string();
    constexpr char digits[] = "0123456789ABCDEF";
    std::string result{"file:///"};
    for (const auto raw : source) {
        const auto character = static_cast<unsigned char>(raw);
        if (std::isalnum(character) != 0 || character == '/' || character == ':' ||
            character == '-' || character == '_' || character == '.') {
            result.push_back(static_cast<char>(character));
        } else {
            result.push_back('%');
            result.push_back(digits[character >> 4U]);
            result.push_back(digits[character & 0x0FU]);
        }
    }
    return result;
}

void apply_theme() {
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowRounding = 7.0F;
    style.ChildRounding = 6.0F;
    style.FrameRounding = 4.0F;
    style.PopupRounding = 6.0F;
    style.ScrollbarRounding = 6.0F;
    style.TabRounding = 4.0F;
    style.WindowPadding = {12.0F, 10.0F};
    style.FramePadding = {8.0F, 5.0F};
    style.ItemSpacing = {8.0F, 7.0F};
    auto& colors = style.Colors;
    colors[ImGuiCol_WindowBg] = {0.045F, 0.052F, 0.072F, 1.0F};
    colors[ImGuiCol_ChildBg] = {0.058F, 0.066F, 0.09F, 1.0F};
    colors[ImGuiCol_FrameBg] = {0.10F, 0.11F, 0.15F, 1.0F};
    colors[ImGuiCol_FrameBgHovered] = {0.15F, 0.17F, 0.22F, 1.0F};
    colors[ImGuiCol_Button] = {0.45F, 0.12F, 0.16F, 1.0F};
    colors[ImGuiCol_ButtonHovered] = {0.62F, 0.17F, 0.21F, 1.0F};
    colors[ImGuiCol_ButtonActive] = {0.74F, 0.23F, 0.27F, 1.0F};
    colors[ImGuiCol_Header] = {0.36F, 0.11F, 0.16F, 1.0F};
    colors[ImGuiCol_HeaderHovered] = {0.53F, 0.16F, 0.21F, 1.0F};
    colors[ImGuiCol_TabSelected] = {0.45F, 0.12F, 0.16F, 1.0F};
    colors[ImGuiCol_CheckMark] = {1.0F, 0.48F, 0.36F, 1.0F};
    colors[ImGuiCol_SliderGrab] = {0.91F, 0.34F, 0.28F, 1.0F};
}

struct BackgroundTask {
    std::future<ToolResult> future;
    ToolResult latest{
        .success = true,
        .summary = "Ready",
        .details = {},
    };

    [[nodiscard]] bool running() const {
        return future.valid() &&
               future.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
    }

    template <typename Function> void start(Function&& function) {
        if (running()) {
            return;
        }
        if (future.valid()) {
            collect();
        }
        latest = {
            .success = true,
            .summary = "Working...",
            .details = {},
        };
        try {
            future = std::async(std::launch::async, std::forward<Function>(function));
        } catch (const std::exception& exception) {
            latest = {
                .success = false,
                .summary = "Could not start background task",
                .details = exception.what(),
            };
        }
    }

    void poll() {
        if (future.valid() &&
            future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            collect();
        }
    }

    void collect() {
        try {
            latest = future.get();
        } catch (const std::exception& exception) {
            latest = {
                .success = false,
                .summary = "Background task failed",
                .details = exception.what(),
            };
        } catch (...) {
            latest = {
                .success = false,
                .summary = "Background task failed",
                .details = "Unknown error",
            };
        }
    }
};

std::string path_utf8(const std::filesystem::path& path) {
    const auto encoded = path.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

std::filesystem::path executable_directory(const std::filesystem::path& executable) {
    if (const auto* base_path = SDL_GetBasePath(); base_path != nullptr && base_path[0] != '\0') {
        return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(base_path)));
    }
    std::error_code error;
    auto absolute = std::filesystem::absolute(executable, error);
    if (error) {
        absolute = executable;
    }
    return absolute.has_parent_path() ? absolute.parent_path() : std::filesystem::current_path();
}

std::filesystem::path sibling_executable(const std::filesystem::path& directory,
                                         std::string_view name) {
    std::string filename(name);
#if defined(_WIN32)
    filename += ".exe";
#endif
    return directory / filename;
}

struct ManagedChild {
    SDL_Process* process{};
    std::string label;
    int exit_code{};
    bool reports_exit_code{};

    [[nodiscard]] bool running() const noexcept {
        return process != nullptr;
    }
};

struct ProcessStartResult {
    SDL_Process* process{};
    std::string error;
};

ProcessStartResult start_process(std::span<const std::string> arguments,
                                 const std::filesystem::path& working_directory,
                                 bool background = true) {
    if (arguments.empty() || arguments.front().empty()) {
        return {
            .process = nullptr,
            .error = "No executable was provided.",
        };
    }

    std::vector<const char*> argument_pointers;
    argument_pointers.reserve(arguments.size() + 1U);
    for (const auto& argument : arguments) {
        argument_pointers.push_back(argument.c_str());
    }
    argument_pointers.push_back(nullptr);
    const auto working_directory_utf8 = path_utf8(working_directory);

    const auto properties = SDL_CreateProperties();
    if (properties == 0U) {
        return {
            .process = nullptr,
            .error = "Could not allocate process settings: " + std::string(SDL_GetError()),
        };
    }
    const auto null_io = static_cast<Sint64>(SDL_PROCESS_STDIO_NULL);
    const bool configured =
        SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
                               argument_pointers.data()) &&
        SDL_SetStringProperty(properties, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING,
                              working_directory_utf8.c_str()) &&
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, null_io) &&
        SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, null_io) &&
        SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, background);
    auto* process = configured ? SDL_CreateProcessWithProperties(properties) : nullptr;
    const auto error = process == nullptr ? std::string(SDL_GetError()) : std::string{};
    SDL_DestroyProperties(properties);
    return {
        .process = process,
        .error = error,
    };
}

std::optional<int> poll_process(ManagedChild& child) {
    if (child.process == nullptr) {
        return std::nullopt;
    }
    int exit_code = 0;
    if (!SDL_WaitProcess(child.process, false, &exit_code)) {
        return std::nullopt;
    }
    SDL_DestroyProcess(child.process);
    child.process = nullptr;
    child.exit_code = exit_code;
    return exit_code;
}

void stop_process(ManagedChild& child, bool force) {
    if (child.process != nullptr) {
        SDL_KillProcess(child.process, force);
    }
}

void shutdown_process(ManagedChild& child) {
    if (child.process == nullptr) {
        return;
    }
    int exit_code = 0;
    bool exited = SDL_WaitProcess(child.process, false, &exit_code);
    if (!exited) {
        SDL_KillProcess(child.process, false);
        SDL_Delay(20);
        exited = SDL_WaitProcess(child.process, false, &exit_code);
    }
    if (!exited) {
        SDL_KillProcess(child.process, true);
        SDL_WaitProcess(child.process, true, &exit_code);
    }
    SDL_DestroyProcess(child.process);
    child.process = nullptr;
    child.exit_code = exit_code;
}

struct FileStamp {
    std::uintmax_t size{};
    std::filesystem::file_time_type last_write_time{};

    bool operator==(const FileStamp&) const = default;
};

std::optional<FileStamp> file_stamp(const ProjectBrowser& browser,
                                    const std::filesystem::path& relative_path) {
    const auto found = std::find_if(browser.entries().begin(), browser.entries().end(),
                                    [&relative_path](const ProjectEntry& entry) {
                                        return entry.relative_path == relative_path &&
                                               entry.kind != ProjectFileKind::Directory;
                                    });
    if (found == browser.entries().end()) {
        return std::nullopt;
    }
    return FileStamp{
        .size = found->size,
        .last_write_time = found->last_write_time,
    };
}

struct EditorState {
    explicit EditorState(const std::filesystem::path& executable)
        : runtime_root(executable_directory(executable)),
          template_root(meat2d::tools::locate_template_root(executable)), manager(template_root) {
        const auto default_parent = std::filesystem::current_path() / "Meat2DProjects";
        new_directory = (default_parent / "my-meat2d-game").string();
        open_directory = std::filesystem::current_path().string();
        next_project_scan = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    }

    std::filesystem::path runtime_root;
    std::filesystem::path template_root;
    ProjectManager manager;
    ProjectBrowser browser;
    BackgroundTask task;
    meat2d::net::LanServerBrowser lan_browser;
    meat2d::net::PublicServerBrowser public_browser;

    std::string new_name{"My Meat2D Game"};
    std::string new_directory;
    std::string open_directory;
    int new_template{};

    std::filesystem::path selected_path;
    ProjectFileKind selected_kind{ProjectFileKind::Other};
    std::string editor_text;
    std::string saved_text;
    std::string disk_text;
    std::string search;
    int browser_filter{};
    std::string new_file{"src/new_file.cpp"};
    std::string import_source;
    std::string import_folder;
    std::string status{"Ready"};

    SDL_Texture* preview_texture{};
    float preview_width{};
    float preview_height{};
    meat2d::assets::SpriteSheet sprite_sheet;
    std::filesystem::path sprite_metadata_path;
    std::string saved_sprite_metadata;
    std::string sprite_metadata_disk_text;
    int preview_animation{};

    std::string repository;
    std::string description;
    bool private_repository{};

    int lan_port{meat2d::net::default_lan_discovery_port};
    std::string directory_host{"127.0.0.1"};
    int directory_port{meat2d::net::default_directory_port};
    int host_port{meat2d::net::default_port};
    int direct_port{meat2d::net::default_port};
    std::string direct_host{"127.0.0.1"};
    std::string player_name{"Developer"};
    std::string session_name{"Meat2D Elements Lab"};
    bool advertise_public{};
    ManagedChild hosted_server;
    std::vector<ManagedChild> launched_clients;

    std::optional<FileStamp> selected_stamp;
    std::optional<FileStamp> metadata_stamp;
    bool external_change_pending{};
    bool selected_missing{};
    std::chrono::steady_clock::time_point next_project_scan{};

    [[nodiscard]] bool text_dirty() const {
        return !selected_path.empty() && ProjectBrowser::is_code(selected_kind) &&
               editor_text != saved_text;
    }

    [[nodiscard]] bool sprite_dirty() const {
        return !selected_path.empty() && selected_kind == ProjectFileKind::Image &&
               preview_texture != nullptr &&
               meat2d::assets::encode_sprite_sheet_toml(sprite_sheet) != saved_sprite_metadata;
    }

    [[nodiscard]] bool dirty() const {
        return text_dirty() || sprite_dirty();
    }

    [[nodiscard]] std::filesystem::path runtime_executable(std::string_view name) const {
        return sibling_executable(runtime_root, name);
    }

    [[nodiscard]] meat2d::assets::SpriteSheet default_sprite_sheet() const {
        return {
            .image = selected_path.generic_string(),
            .frame_width = static_cast<std::uint16_t>(std::clamp(preview_width, 1.0F, 32.0F)),
            .frame_height = static_cast<std::uint16_t>(std::clamp(preview_height, 1.0F, 32.0F)),
            .margin = 0,
            .spacing = 0,
            .animations = {},
        };
    }

    void capture_file_stamps() {
        selected_stamp = selected_path.empty() ? std::nullopt : file_stamp(browser, selected_path);
        metadata_stamp =
            sprite_metadata_path.empty() ? std::nullopt : file_stamp(browser, sprite_metadata_path);
    }

    void release_preview() {
        if (preview_texture != nullptr) {
            SDL_DestroyTexture(preview_texture);
            preview_texture = nullptr;
        }
        preview_width = 0.0F;
        preview_height = 0.0F;
    }

    bool open_project(const std::filesystem::path& path) {
        if (dirty()) {
            status = "Save or revert the current code or sprite settings before changing "
                     "projects.";
            return false;
        }
        release_preview();
        selected_path.clear();
        editor_text.clear();
        saved_text.clear();
        disk_text.clear();
        saved_sprite_metadata.clear();
        sprite_metadata_disk_text.clear();
        sprite_metadata_path.clear();
        selected_stamp.reset();
        metadata_stamp.reset();
        external_change_pending = false;
        selected_missing = false;
        if (!browser.open(path)) {
            status = std::string(browser.last_error());
            return false;
        }
        open_directory = browser.root().string();
        next_project_scan = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        status = "Opened " + browser.root().string();
        return true;
    }

    void select_file(const ProjectEntry& entry, SDL_Renderer* renderer,
                     bool discard_unsaved = false) {
        if (entry.kind == ProjectFileKind::Directory) {
            return;
        }
        if (dirty() && !discard_unsaved) {
            status = "Save or revert changes before opening another file.";
            return;
        }
        release_preview();
        selected_path = entry.relative_path;
        selected_kind = entry.kind;
        editor_text.clear();
        saved_text.clear();
        disk_text.clear();
        sprite_metadata_path.clear();
        saved_sprite_metadata.clear();
        sprite_metadata_disk_text.clear();
        external_change_pending = false;
        selected_missing = false;
        capture_file_stamps();

        if (entry.editable) {
            auto loaded = browser.load_text(entry.relative_path);
            if (!loaded.success) {
                status = loaded.error;
                return;
            }
            editor_text = std::move(loaded.text);
            saved_text = editor_text;
            disk_text = editor_text;
            status = "Opened " + entry.relative_path.generic_string();
            return;
        }
        if (entry.kind != ProjectFileKind::Image) {
            status = "Selected " + entry.relative_path.generic_string();
            return;
        }

        const auto resolved = browser.resolve_for_external_open(entry.relative_path);
        if (!resolved.success) {
            status = resolved.message;
            return;
        }
        if (!reload_preview_texture(renderer)) {
            return;
        }
        sprite_sheet = default_sprite_sheet();
        sprite_metadata_path = entry.relative_path.parent_path() /
                               (entry.relative_path.stem().string() + ".sprite.toml");
        const auto metadata = browser.load_text(sprite_metadata_path);
        if (metadata.success) {
            sprite_metadata_disk_text = metadata.text;
            const auto parsed = meat2d::assets::decode_sprite_sheet_toml(metadata.text);
            if (parsed.sheet) {
                sprite_sheet = *parsed.sheet;
            } else {
                saved_sprite_metadata = meat2d::assets::encode_sprite_sheet_toml(sprite_sheet);
                capture_file_stamps();
                status = "Sprite metadata line " + std::to_string(parsed.error_line) + ": " +
                         parsed.error;
                return;
            }
        }
        saved_sprite_metadata = meat2d::assets::encode_sprite_sheet_toml(sprite_sheet);
        capture_file_stamps();
        status = "Previewing " + entry.relative_path.generic_string();
    }

    bool save_editor() {
        if (selected_path.empty() || !ProjectBrowser::is_code(selected_kind)) {
            return false;
        }
        if (external_change_pending) {
            status = "Resolve the external file conflict before saving.";
            return false;
        }
        const auto result = browser.save_text(selected_path, editor_text);
        status = result.message;
        if (result.success) {
            saved_text = editor_text;
            disk_text = editor_text;
            external_change_pending = false;
            selected_missing = false;
            capture_file_stamps();
        }
        return result.success;
    }

    bool save_sprite_metadata() {
        if (external_change_pending) {
            status = "Resolve the external sprite conflict before saving.";
            return false;
        }
        const auto width = static_cast<std::uint32_t>(std::max(preview_width, 0.0F));
        const auto height = static_cast<std::uint32_t>(std::max(preview_height, 0.0F));
        if (!meat2d::assets::valid_sprite_sheet(sprite_sheet, width, height)) {
            status = "Sprite settings or animation frame ranges are invalid.";
            return false;
        }
        const auto encoded = meat2d::assets::encode_sprite_sheet_toml(sprite_sheet);
        BrowserResult result;
        if (browser.resolve_for_external_open(sprite_metadata_path).success) {
            result = browser.save_text(sprite_metadata_path, encoded);
        } else {
            result = browser.create_text_file(sprite_metadata_path, encoded);
        }
        status = result.message;
        if (result.success) {
            saved_sprite_metadata = encoded;
            sprite_metadata_disk_text = encoded;
            external_change_pending = false;
            selected_missing = false;
            capture_file_stamps();
        }
        return result.success;
    }

    bool save_active_document() {
        if (external_change_pending) {
            status = "Resolve the external file conflict before building or publishing.";
            return false;
        }
        if (text_dirty()) {
            return save_editor();
        }
        if (sprite_dirty()) {
            return save_sprite_metadata();
        }
        return true;
    }

    bool reload_preview_texture(SDL_Renderer* renderer) {
        const auto resolved = browser.resolve_for_external_open(selected_path);
        if (!resolved.success) {
            status = resolved.message;
            return false;
        }
        const auto image_path = path_utf8(resolved.path);
        auto* texture = IMG_LoadTexture(renderer, image_path.c_str());
        float width = 0.0F;
        float height = 0.0F;
        if (texture == nullptr || !SDL_GetTextureSize(texture, &width, &height)) {
            if (texture != nullptr) {
                SDL_DestroyTexture(texture);
            }
            status = "Image preview failed: " + std::string(SDL_GetError());
            return false;
        }
        release_preview();
        preview_texture = texture;
        preview_width = width;
        preview_height = height;
        return true;
    }

    bool reload_selected(SDL_Renderer* renderer) {
        const auto path = selected_path;
        const auto found = std::find_if(
            browser.entries().begin(), browser.entries().end(),
            [&path](const ProjectEntry& entry) { return entry.relative_path == path; });
        if (found == browser.entries().end() || found->kind == ProjectFileKind::Directory) {
            status = "The selected file no longer exists in the project.";
            selected_missing = true;
            external_change_pending = true;
            return false;
        }
        select_file(*found, renderer, true);
        return true;
    }

    bool recreate_selected() {
        if (selected_path.empty() || !ProjectBrowser::is_code(selected_kind)) {
            return false;
        }
        const auto result = browser.create_text_file(selected_path, editor_text);
        status = result.message;
        if (result.success) {
            saved_text = editor_text;
            disk_text = editor_text;
            external_change_pending = false;
            selected_missing = false;
            capture_file_stamps();
        }
        return result.success;
    }

    void close_selected() {
        release_preview();
        selected_path.clear();
        editor_text.clear();
        saved_text.clear();
        disk_text.clear();
        sprite_metadata_path.clear();
        saved_sprite_metadata.clear();
        sprite_metadata_disk_text.clear();
        selected_stamp.reset();
        metadata_stamp.reset();
        external_change_pending = false;
        selected_missing = false;
    }

    void refresh_project(SDL_Renderer* renderer, bool report_unchanged = false) {
        if (!browser.is_open()) {
            return;
        }
        next_project_scan = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        if (!browser.refresh()) {
            status = std::string(browser.last_error());
            return;
        }
        if (selected_path.empty()) {
            if (report_unchanged) {
                status = browser.last_error().empty() ? "Project files refreshed."
                                                      : std::string(browser.last_error());
            }
            return;
        }

        const auto current_selected_stamp = file_stamp(browser, selected_path);
        const auto current_metadata_stamp = sprite_metadata_path.empty()
                                                ? std::optional<FileStamp>{}
                                                : file_stamp(browser, sprite_metadata_path);
        const bool was_selected_missing = selected_missing;
        if (!current_selected_stamp) {
            selected_stamp.reset();
            metadata_stamp = current_metadata_stamp;
            selected_missing = true;
            external_change_pending = true;
            status = "The selected file was deleted outside the editor.";
            return;
        }

        if (ProjectBrowser::is_code(selected_kind)) {
            const auto loaded = browser.load_text(selected_path);
            selected_stamp = current_selected_stamp;
            metadata_stamp = current_metadata_stamp;
            if (!loaded.success) {
                external_change_pending = true;
                status = loaded.error;
                return;
            }
            if (was_selected_missing || loaded.text != disk_text) {
                const bool had_local_text_changes = text_dirty();
                disk_text = loaded.text;
                if (loaded.text == editor_text) {
                    saved_text = editor_text;
                    external_change_pending = false;
                    selected_missing = false;
                    status = "The external editor saved the current buffer.";
                    return;
                }
                if (had_local_text_changes) {
                    saved_text = loaded.text;
                    external_change_pending = true;
                    selected_missing = false;
                    status = "The selected file changed on disk; choose which version to keep.";
                } else {
                    editor_text = loaded.text;
                    saved_text = loaded.text;
                    external_change_pending = false;
                    selected_missing = false;
                    status =
                        "Reloaded " + selected_path.generic_string() + " after an external edit.";
                }
                return;
            }
        } else if (selected_kind == ProjectFileKind::Image) {
            std::string current_metadata_text;
            const auto metadata = browser.load_text(sprite_metadata_path);
            if (metadata.success) {
                current_metadata_text = metadata.text;
            }
            const bool image_changed = current_selected_stamp != selected_stamp;
            const bool metadata_existence_changed =
                current_metadata_stamp.has_value() != metadata_stamp.has_value();
            const bool metadata_changed = current_metadata_stamp != metadata_stamp ||
                                          current_metadata_text != sprite_metadata_disk_text;
            const bool had_local_sprite_changes = sprite_dirty();
            selected_stamp = current_selected_stamp;
            metadata_stamp = current_metadata_stamp;
            sprite_metadata_disk_text = std::move(current_metadata_text);
            if (image_changed || metadata_changed) {
                if (had_local_sprite_changes) {
                    if (image_changed && !reload_preview_texture(renderer)) {
                        external_change_pending = true;
                        return;
                    }
                    if (image_changed) {
                        selected_missing = false;
                    }
                    if (metadata_changed) {
                        const auto previous_disk_settings = saved_sprite_metadata;
                        auto disk_settings = default_sprite_sheet();
                        bool disk_metadata_valid = !current_metadata_stamp.has_value();
                        if (metadata.success) {
                            const auto parsed =
                                meat2d::assets::decode_sprite_sheet_toml(metadata.text);
                            disk_metadata_valid = parsed.sheet.has_value();
                            if (parsed.sheet) {
                                disk_settings = *parsed.sheet;
                            }
                        }
                        saved_sprite_metadata =
                            meat2d::assets::encode_sprite_sheet_toml(disk_settings);
                        const auto editor_settings =
                            meat2d::assets::encode_sprite_sheet_toml(sprite_sheet);
                        if (editor_settings == saved_sprite_metadata) {
                            external_change_pending = false;
                            selected_missing = false;
                            status = "The external editor saved the current sprite settings.";
                        } else if (!disk_metadata_valid || metadata_existence_changed ||
                                   saved_sprite_metadata != previous_disk_settings) {
                            external_change_pending = true;
                            selected_missing = false;
                            status =
                                "Sprite metadata changed on disk; choose which version to keep. "
                                "The image preview is current.";
                        } else {
                            external_change_pending = false;
                            selected_missing = false;
                            status = "Reloaded metadata formatting; unsaved sprite settings were "
                                     "preserved.";
                        }
                    } else {
                        external_change_pending = false;
                        status = "Reloaded the image; unsaved sprite settings were preserved.";
                    }
                } else {
                    reload_selected(renderer);
                }
                return;
            }
        } else if (current_selected_stamp != selected_stamp) {
            selected_stamp = current_selected_stamp;
            external_change_pending = false;
            selected_missing = false;
            status = "The selected asset changed on disk.";
            return;
        }

        selected_stamp = current_selected_stamp;
        metadata_stamp = current_metadata_stamp;
        if (report_unchanged) {
            status = browser.last_error().empty() ? "Project files refreshed."
                                                  : std::string(browser.last_error());
        }
    }

    void poll_project_changes(SDL_Renderer* renderer) {
        if (browser.is_open() && std::chrono::steady_clock::now() >= next_project_scan) {
            refresh_project(renderer);
        }
    }

    bool launch_client(std::vector<std::string> arguments, std::string label,
                       bool background = true) {
        poll_session_processes();
        constexpr std::size_t maximum_editor_clients = 8;
        if (launched_clients.size() >= maximum_editor_clients) {
            status = "Close a launched client before starting another (editor limit: 8).";
            return false;
        }
        const auto executable = runtime_executable("meat2d_sandbox");
        std::error_code error;
        if (!std::filesystem::is_regular_file(executable, error)) {
            status = "Living Lab client was not found beside the editor: " + executable.string();
            return false;
        }
        arguments.insert(arguments.begin(), path_utf8(executable));
        const auto working_directory = browser.is_open() ? browser.root() : runtime_root;
        auto started = start_process(arguments, working_directory, background);
        if (started.process == nullptr) {
            status = "Could not launch client: " + started.error;
            return false;
        }
        launched_clients.push_back({
            .process = started.process,
            .label = std::move(label),
            .exit_code = 0,
            .reports_exit_code = !background,
        });
        status = "Launched " + launched_clients.back().label + '.';
        return true;
    }

    bool join_direct(std::string host, int port, bool background = true) {
        if (host.empty() || player_name.empty() ||
            player_name.size() > meat2d::net::maximum_player_name_bytes) {
            status = "Enter a host and a player name of 1-24 bytes.";
            return false;
        }
        const auto safe_port = std::clamp(port, 1, 65'535);
        return launch_client(
            {
                "--connect",
                host,
                "--port",
                std::to_string(safe_port),
                "--name",
                player_name,
            },
            host + ':' + std::to_string(safe_port), background);
    }

    bool join_public(std::uint64_t server_id, bool background = true) {
        if (server_id == 0U || directory_host.empty() || player_name.empty() ||
            player_name.size() > meat2d::net::maximum_player_name_bytes) {
            status = "Enter a directory, server, and player name of 1-24 bytes.";
            return false;
        }
        const auto safe_directory_port = std::clamp(directory_port, 1, 65'535);
        return launch_client(
            {
                "--directory",
                directory_host,
                "--directory-port",
                std::to_string(safe_directory_port),
                "--server-id",
                std::to_string(server_id),
                "--name",
                player_name,
            },
            "public server " + std::to_string(server_id), background);
    }

    bool start_host(bool process_smoke_test = false, bool background = true) {
        poll_session_processes();
        if (hosted_server.running()) {
            status = "A server launched by this editor is already running.";
            return false;
        }
        if (!process_smoke_test && (session_name.empty() ||
                                    session_name.size() > meat2d::net::maximum_server_name_bytes)) {
            status = "Enter a session name of 1-48 bytes.";
            return false;
        }
        const auto executable = runtime_executable("meat2d_server");
        std::error_code error;
        if (!std::filesystem::is_regular_file(executable, error)) {
            status = "Dedicated server was not found beside the editor: " + executable.string();
            return false;
        }

        const auto safe_host_port = process_smoke_test ? 0 : std::clamp(host_port, 1, 65'535);
        std::vector<std::string> arguments{
            path_utf8(executable),
            "--listen",
            "--port",
            std::to_string(safe_host_port),
            "--name",
            process_smoke_test ? "Editor Process Smoke" : session_name,
        };
        if (process_smoke_test) {
            arguments.insert(arguments.end(), {"--no-lan", "--ticks", "3", "--fast"});
        } else {
            arguments.insert(arguments.end(),
                             {"--discovery-port", std::to_string(std::clamp(lan_port, 1, 65'535))});
            if (advertise_public) {
                if (directory_host.empty()) {
                    status = "Enter a public directory host before advertising publicly.";
                    return false;
                }
                arguments.insert(arguments.end(),
                                 {"--public-directory", directory_host, "--directory-port",
                                  std::to_string(std::clamp(directory_port, 1, 65'535))});
            }
        }

        const auto working_directory = browser.is_open() ? browser.root() : runtime_root;
        auto started = start_process(arguments, working_directory, background);
        if (started.process == nullptr) {
            status = "Could not launch dedicated server: " + started.error;
            return false;
        }
        hosted_server = {
            .process = started.process,
            .label = process_smoke_test ? "process smoke server" : session_name,
            .exit_code = 0,
            .reports_exit_code = !background,
        };
        status = process_smoke_test ? "Started the editor process smoke server."
                                    : "Hosting " + session_name + " on UDP " +
                                          std::to_string(std::clamp(host_port, 1, 65'535)) + '.';
        return true;
    }

    void poll_session_processes() {
        if (const auto exit_code = poll_process(hosted_server)) {
            status =
                hosted_server.label + " stopped" +
                (hosted_server.reports_exit_code ? " (exit " + std::to_string(*exit_code) + ")."
                                                 : ".");
        }
        for (auto client = launched_clients.begin(); client != launched_clients.end();) {
            if (const auto exit_code = poll_process(*client)) {
                status = client->label + " closed" +
                         (client->reports_exit_code ? " (exit " + std::to_string(*exit_code) + ")."
                                                    : ".");
                client = launched_clients.erase(client);
            } else {
                ++client;
            }
        }
    }

    void stop_host(bool force = false) {
        if (hosted_server.running()) {
            stop_process(hosted_server, force);
            status = force ? "Force-stopping the hosted server." : "Stopping the hosted server.";
        }
    }

    void stop_clients(bool force = false) {
        for (auto& client : launched_clients) {
            stop_process(client, force);
        }
        if (!launched_clients.empty()) {
            status = force ? "Force-stopping launched clients." : "Stopping launched clients.";
        }
    }

    void shutdown_session_processes() {
        shutdown_process(hosted_server);
        for (auto& client : launched_clients) {
            shutdown_process(client);
        }
        launched_clients.clear();
    }
};

void poll_dialogs(EditorState& state) {
    std::scoped_lock lock(dialog_inbox.mutex);
    if (dialog_inbox.new_project_parent) {
        state.new_directory = (std::filesystem::path(*dialog_inbox.new_project_parent) /
                               ProjectManager::project_slug(state.new_name))
                                  .string();
        dialog_inbox.new_project_parent.reset();
    }
    if (dialog_inbox.open_project) {
        state.open_directory = *dialog_inbox.open_project;
        dialog_inbox.open_project.reset();
    }
    if (dialog_inbox.import_asset) {
        state.import_source = *dialog_inbox.import_asset;
        dialog_inbox.import_asset.reset();
    }
    if (dialog_inbox.failed) {
        state.status = "The operating-system file dialog failed.";
        dialog_inbox.failed = false;
    }
}

void draw_start(EditorState& state, SDL_Window* window) {
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 36.0F);
    const auto available = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(std::max(20.0F, (available - 720.0F) * 0.5F));
    ImGui::BeginChild("welcome", {std::min(720.0F, available), 0.0F}, ImGuiChildFlags_Borders);
    ImGui::TextUnformatted("Meat2D Engine");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", meat2d::version_string.data());
    ImGui::SeparatorText("Create a game");
    ImGui::TextWrapped("Start from a simulation-ready side-view or top-down C++ project.");
    ImGui::InputText("Project name", &state.new_name);
    ImGui::SetNextItemWidth(-90.0F);
    ImGui::InputText("Destination", &state.new_directory);
    ImGui::SameLine();
    if (ImGui::Button("Browse##new")) {
        SDL_ShowOpenFolderDialog(new_project_folder_callback, nullptr, window, nullptr, false);
    }
    const char* templates[] = {"Side scroller", "Top-down shooter"};
    ImGui::Combo("Starter", &state.new_template, templates, 2);
    if (ImGui::Button("Create and open", {160.0F, 0.0F})) {
        const auto result = state.manager.create_project({
            .name = state.new_name,
            .directory = state.new_directory,
            .project_template = state.new_template == 0
                                    ? meat2d::tools::ProjectTemplate::SideScroller
                                    : meat2d::tools::ProjectTemplate::TopDown,
            .engine_git_tag = "main",
        });
        state.status = result.summary + (result.details.empty() ? "" : ": " + result.details);
        if (result.success) {
            state.open_project(state.new_directory);
        }
    }
    if (!state.manager.templates_available()) {
        ImGui::TextColored({1.0F, 0.42F, 0.35F, 1.0F},
                           "Templates were not found. Set MEAT2D_TEMPLATE_ROOT.");
    }

    ImGui::Spacing();
    ImGui::SeparatorText("Open an existing project");
    ImGui::SetNextItemWidth(-90.0F);
    ImGui::InputText("Project folder", &state.open_directory);
    ImGui::SameLine();
    if (ImGui::Button("Browse##open")) {
        SDL_ShowOpenFolderDialog(open_project_folder_callback, nullptr, window, nullptr, false);
    }
    if (ImGui::Button("Open project", {160.0F, 0.0F})) {
        state.open_project(state.open_directory);
    }
    ImGui::Spacing();
    ImGui::TextWrapped("%s", state.status.c_str());
    ImGui::EndChild();
}

void draw_task_output(EditorState& state) {
    state.task.poll();
    const auto running = state.task.running();
    ImGui::SeparatorText("Build output");
    if (running) {
        ImGui::TextColored({1.0F, 0.72F, 0.25F, 1.0F}, "Working... the editor remains responsive.");
    } else {
        ImGui::TextColored(state.task.latest.success ? ImVec4{0.38F, 0.88F, 0.52F, 1.0F}
                                                     : ImVec4{1.0F, 0.38F, 0.34F, 1.0F},
                           "%s", state.task.latest.summary.c_str());
    }
    ImGui::BeginChild("task-log", {0.0F, 220.0F}, ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::TextUnformatted(state.task.latest.details.c_str());
    ImGui::EndChild();
}

void draw_overview(EditorState& state) {
    ImGui::Text("Project: %s", state.browser.root().filename().string().c_str());
    ImGui::TextDisabled("%s", state.browser.root().string().c_str());
    ImGui::Spacing();
    const bool busy = state.task.running();
    ImGui::BeginDisabled(busy);
    if (ImGui::Button("Build Debug", {130.0F, 0.0F})) {
        if (state.save_active_document()) {
            const auto root = state.browser.root();
            state.task.start(
                [&state, root] { return state.manager.build_project(root, BuildProfile::Debug); });
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Build & Test", {130.0F, 0.0F})) {
        if (state.save_active_document()) {
            const auto root = state.browser.root();
            state.task.start([&state, root] { return state.manager.run_project(root); });
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Build Release", {130.0F, 0.0F})) {
        if (state.save_active_document()) {
            const auto root = state.browser.root();
            state.task.start([&state, root] {
                return state.manager.build_project(root, BuildProfile::Release);
            });
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Package", {110.0F, 0.0F})) {
        if (state.save_active_document()) {
            const auto root = state.browser.root();
            state.task.start([&state, root] { return state.manager.package_project(root); });
        }
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("Build & Test compiles the Debug preset and launches the game. "
                       "Close the game window to return its output and exit status here.");

    ImGui::SeparatorText("Publish");
    ImGui::InputTextWithHint("Repository", "OWNER/game-repository", &state.repository);
    ImGui::InputText("Description", &state.description);
    ImGui::Checkbox("Private repository", &state.private_repository);
    ImGui::BeginDisabled(busy || state.repository.empty());
    if (ImGui::Button("Create/push GitHub repository", {230.0F, 0.0F})) {
        if (state.save_active_document()) {
            const auto root = state.browser.root();
            const auto repository = state.repository;
            const auto description = state.description;
            const auto visibility = state.private_repository
                                        ? meat2d::tools::RepositoryVisibility::Private
                                        : meat2d::tools::RepositoryVisibility::Public;
            state.task.start([&state, root, repository, description, visibility] {
                return state.manager.publish_project({
                    .project_directory = root,
                    .repository = repository,
                    .description = description,
                    .visibility = visibility,
                });
            });
        }
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("Publishing stages and commits project files, then uses the authenticated "
                       "GitHub CLI to create or push the selected repository.");
    draw_task_output(state);
}

bool entry_visible(const EditorState& state, const ProjectEntry& entry) {
    if (!contains_case_insensitive(entry.relative_path.generic_string(), state.search)) {
        return false;
    }
    if (entry.kind == ProjectFileKind::Directory || state.browser_filter == 0) {
        return true;
    }
    if (state.browser_filter == 1) {
        return ProjectBrowser::is_code(entry.kind);
    }
    return ProjectBrowser::is_asset(entry.kind);
}

void draw_file_list(EditorState& state, SDL_Renderer* renderer, SDL_Window* window) {
    ImGui::InputTextWithHint("##search", "Search project files", &state.search);
    ImGui::SameLine();
    const char* browser_filters[] = {"All", "Code", "Assets"};
    ImGui::SetNextItemWidth(90.0F);
    ImGui::Combo("##filter", &state.browser_filter, browser_filters, 3);
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        state.refresh_project(renderer, true);
    }
    bool generated = state.browser.show_generated();
    if (ImGui::Checkbox("Show generated folders", &generated)) {
        state.browser.set_show_generated(generated);
        state.refresh_project(renderer, true);
    }

    ImGui::BeginChild("project-tree", {0.0F, -174.0F}, ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);
    for (const auto& entry : state.browser.entries()) {
        if (!entry_visible(state, entry)) {
            continue;
        }
        const auto prefix = entry.kind == ProjectFileKind::Directory ? "[DIR] " : "      ";
        const auto label =
            std::string(entry.depth * 2U, ' ') + prefix + entry.relative_path.generic_string();
        const bool selected = entry.relative_path == state.selected_path;
        if (entry.kind == ProjectFileKind::Directory) {
            ImGui::TextDisabled("%s", label.c_str());
        } else if (ImGui::Selectable(label.c_str(), selected,
                                     ImGuiSelectableFlags_AllowDoubleClick)) {
            state.select_file(entry, renderer);
        }
        if (ImGui::IsItemHovered() && entry.kind != ProjectFileKind::Directory) {
            ImGui::SetTooltip("%s - %s", ProjectBrowser::kind_name(entry.kind).data(),
                              byte_size(entry.size).c_str());
        }
    }
    ImGui::EndChild();

    ImGui::SeparatorText("Create / import");
    ImGui::SetNextItemWidth(-95.0F);
    ImGui::InputText("##new-file", &state.new_file);
    ImGui::SameLine();
    if (ImGui::Button("New file", {85.0F, 0.0F})) {
        const auto result = state.browser.create_text_file(state.new_file);
        state.status = result.message;
    }
    ImGui::SetNextItemWidth(-180.0F);
    ImGui::InputTextWithHint("##asset-source", "Absolute path to PNG, audio, font, or other asset",
                             &state.import_source);
    ImGui::SameLine();
    if (ImGui::Button("Browse##asset", {78.0F, 0.0F})) {
        static constexpr SDL_DialogFileFilter asset_filters[]{
            {"Game assets", "png;jpg;jpeg;bmp;gif;webp;tga;wav;ogg;mp3;flac;ttf;otf"},
            {"Images", "png;jpg;jpeg;bmp;gif;webp;tga"},
            {"All files", "*"},
        };
        SDL_ShowOpenFileDialog(import_asset_callback, nullptr, window, asset_filters,
                               static_cast<int>(std::size(asset_filters)), nullptr, false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Import", {85.0F, 0.0F})) {
        const auto result = state.browser.import_asset(state.import_source, state.import_folder);
        state.status = result.message;
    }
    ImGui::InputTextWithHint("Asset subfolder", "textures, audio, fonts...", &state.import_folder);
}

void draw_image(SDL_Texture* texture, ImVec2 size, ImVec2 uv0 = {0.0F, 0.0F},
                ImVec2 uv1 = {1.0F, 1.0F}) {
    ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<std::intptr_t>(texture)), size, uv0,
                 uv1);
}

void draw_sprite_grid(EditorState& state) {
    if (state.preview_texture == nullptr) {
        return;
    }
    const auto available_width = std::max(100.0F, ImGui::GetContentRegionAvail().x);
    const auto scale =
        std::min({1.0F, available_width / state.preview_width, 420.0F / state.preview_height});
    const ImVec2 display{
        state.preview_width * scale,
        state.preview_height * scale,
    };
    const auto origin = ImGui::GetCursorScreenPos();
    draw_image(state.preview_texture, display);

    const auto image_width = static_cast<std::uint32_t>(state.preview_width);
    const auto image_height = static_cast<std::uint32_t>(state.preview_height);
    const auto frames =
        meat2d::assets::sprite_frame_count(state.sprite_sheet, image_width, image_height);
    if (frames == 0U || frames > 4'096U) {
        return;
    }
    auto* draw = ImGui::GetWindowDrawList();
    for (std::uint32_t index = 0; index < frames; ++index) {
        const auto frame =
            meat2d::assets::sprite_frame(state.sprite_sheet, image_width, image_height, index);
        if (!frame) {
            continue;
        }
        const ImVec2 minimum{
            origin.x + static_cast<float>(frame->x) * scale,
            origin.y + static_cast<float>(frame->y) * scale,
        };
        const ImVec2 maximum{
            minimum.x + static_cast<float>(frame->width) * scale,
            minimum.y + static_cast<float>(frame->height) * scale,
        };
        draw->AddRect(minimum, maximum, IM_COL32(255, 94, 76, 210), 0.0F, 0, 1.0F);
    }
}

void draw_sprite_manager(EditorState& state) {
    ImGui::Text("%s%s (%d x %d)", state.selected_path.generic_string().c_str(),
                state.sprite_dirty() ? " *" : "", static_cast<int>(state.preview_width),
                static_cast<int>(state.preview_height));
    ImGui::TextDisabled("Metadata: %s", state.sprite_metadata_path.generic_string().c_str());
    draw_sprite_grid(state);

    int frame_width = state.sprite_sheet.frame_width;
    int frame_height = state.sprite_sheet.frame_height;
    int margin = state.sprite_sheet.margin;
    int spacing = state.sprite_sheet.spacing;
    ImGui::SetNextItemWidth(130.0F);
    if (ImGui::InputInt("Frame width", &frame_width)) {
        state.sprite_sheet.frame_width =
            static_cast<std::uint16_t>(std::clamp(frame_width, 1, 65'535));
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130.0F);
    if (ImGui::InputInt("Frame height", &frame_height)) {
        state.sprite_sheet.frame_height =
            static_cast<std::uint16_t>(std::clamp(frame_height, 1, 65'535));
    }
    ImGui::SetNextItemWidth(130.0F);
    if (ImGui::InputInt("Margin", &margin)) {
        state.sprite_sheet.margin = static_cast<std::uint16_t>(std::clamp(margin, 0, 65'535));
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130.0F);
    if (ImGui::InputInt("Spacing", &spacing)) {
        state.sprite_sheet.spacing = static_cast<std::uint16_t>(std::clamp(spacing, 0, 65'535));
    }
    const auto frame_count = meat2d::assets::sprite_frame_count(
        state.sprite_sheet, static_cast<std::uint32_t>(state.preview_width),
        static_cast<std::uint32_t>(state.preview_height));
    ImGui::Text("Detected frames: %u", frame_count);

    ImGui::SeparatorText("Animations");
    std::optional<std::size_t> remove_animation;
    for (std::size_t index = 0; index < state.sprite_sheet.animations.size(); ++index) {
        auto& animation = state.sprite_sheet.animations[index];
        ImGui::PushID(static_cast<int>(index));
        ImGui::InputText("Name", &animation.name);
        int first = static_cast<int>(std::min<std::uint32_t>(
            animation.first_frame, static_cast<std::uint32_t>(std::numeric_limits<int>::max())));
        int count = static_cast<int>(std::min<std::uint32_t>(
            animation.frame_count, static_cast<std::uint32_t>(std::numeric_limits<int>::max())));
        int fps = animation.frames_per_second;
        if (ImGui::InputInt("First frame", &first)) {
            animation.first_frame = static_cast<std::uint32_t>(std::max(first, 0));
        }
        ImGui::SameLine();
        if (ImGui::InputInt("Count", &count)) {
            animation.frame_count = static_cast<std::uint32_t>(std::max(count, 1));
        }
        if (ImGui::InputInt("FPS", &fps)) {
            animation.frames_per_second = static_cast<std::uint16_t>(std::clamp(fps, 1, 65'535));
        }
        ImGui::SameLine();
        ImGui::Checkbox("Loop", &animation.loop);
        ImGui::SameLine();
        if (ImGui::Button("Preview")) {
            state.preview_animation = static_cast<int>(index);
        }
        ImGui::SameLine();
        if (ImGui::Button("Remove")) {
            remove_animation = index;
        }
        ImGui::Separator();
        ImGui::PopID();
    }
    if (remove_animation) {
        state.sprite_sheet.animations.erase(state.sprite_sheet.animations.begin() +
                                            static_cast<std::ptrdiff_t>(*remove_animation));
        state.preview_animation = 0;
    }
    ImGui::BeginDisabled(state.sprite_sheet.animations.size() >=
                         meat2d::assets::maximum_sprite_animations);
    if (ImGui::Button("Add animation")) {
        state.sprite_sheet.animations.push_back({
            .name = "animation-" + std::to_string(state.sprite_sheet.animations.size() + 1U),
            .first_frame = 0,
            .frame_count = std::max<std::uint32_t>(1U, frame_count),
            .frames_per_second = 8,
            .loop = true,
        });
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Save sprite metadata") ||
        (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))) {
        state.save_sprite_metadata();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!state.sprite_dirty() || state.external_change_pending);
    if (ImGui::Button("Revert sprite settings")) {
        const auto parsed = meat2d::assets::decode_sprite_sheet_toml(state.saved_sprite_metadata);
        if (parsed.sheet) {
            state.sprite_sheet = *parsed.sheet;
            state.status = "Unsaved sprite settings reverted.";
        }
    }
    ImGui::EndDisabled();

    if (!state.sprite_sheet.animations.empty() && frame_count != 0U) {
        state.preview_animation = std::clamp(
            state.preview_animation, 0, static_cast<int>(state.sprite_sheet.animations.size()) - 1);
        const auto& animation =
            state.sprite_sheet.animations[static_cast<std::size_t>(state.preview_animation)];
        if (animation.frame_count != 0U && animation.first_frame < frame_count) {
            const auto offset =
                static_cast<std::uint32_t>(ImGui::GetTime() * animation.frames_per_second) %
                animation.frame_count;
            const auto index = std::min(animation.first_frame + offset, frame_count - 1U);
            const auto frame = meat2d::assets::sprite_frame(
                state.sprite_sheet, static_cast<std::uint32_t>(state.preview_width),
                static_cast<std::uint32_t>(state.preview_height), index);
            if (frame) {
                const ImVec2 uv0{
                    static_cast<float>(frame->x) / state.preview_width,
                    static_cast<float>(frame->y) / state.preview_height,
                };
                const ImVec2 uv1{
                    static_cast<float>(frame->x + frame->width) / state.preview_width,
                    static_cast<float>(frame->y + frame->height) / state.preview_height,
                };
                ImGui::Text("Animation preview: %s (frame %u)", animation.name.c_str(), index);
                const auto preview_scale = std::min(
                    6.0F, 192.0F / static_cast<float>(std::max(state.sprite_sheet.frame_width,
                                                               state.sprite_sheet.frame_height)));
                draw_image(state.preview_texture,
                           {
                               state.sprite_sheet.frame_width * preview_scale,
                               state.sprite_sheet.frame_height * preview_scale,
                           },
                           uv0, uv1);
            }
        }
    }
}

void draw_selected_file(EditorState& state, SDL_Renderer* renderer) {
    if (state.selected_path.empty()) {
        ImGui::TextDisabled("Select code, configuration, or an asset from the project browser.");
        return;
    }
    if (state.external_change_pending) {
        ImGui::TextColored(
            {1.0F, 0.68F, 0.24F, 1.0F}, "%s",
            state.selected_missing
                ? "This file was deleted outside the editor. Your in-editor data is preserved."
                : "This file changed outside the editor while local edits were pending.");
        if (state.selected_missing) {
            if (ProjectBrowser::is_code(state.selected_kind) &&
                ImGui::Button("Recreate from editor buffer")) {
                state.recreate_selected();
            }
            if (ProjectBrowser::is_code(state.selected_kind)) {
                ImGui::SameLine();
            }
            if (ImGui::Button(state.dirty() ? "Close and discard buffer" : "Close file")) {
                state.close_selected();
                return;
            }
        } else {
            if (ImGui::Button("Reload disk version")) {
                state.reload_selected(renderer);
            }
            ImGui::SameLine();
            if (ImGui::Button("Keep editor version")) {
                state.external_change_pending = false;
                state.status = "Keeping the editor version; Save will replace the disk version.";
            }
        }
        ImGui::Separator();
    }
    if (ProjectBrowser::is_code(state.selected_kind)) {
        ImGui::Text("%s%s", state.selected_path.generic_string().c_str(),
                    state.text_dirty() ? " *" : "");
        ImGui::SameLine();
        if (ImGui::Button("Save") ||
            (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))) {
            state.save_editor();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!state.text_dirty() || state.external_change_pending);
        if (ImGui::Button("Revert")) {
            state.editor_text = state.disk_text;
            state.saved_text = state.disk_text;
            state.status = "Unsaved changes reverted.";
        }
        ImGui::EndDisabled();
        ImGui::InputTextMultiline("##code-editor", &state.editor_text, {-1.0F, -1.0F},
                                  ImGuiInputTextFlags_AllowTabInput);
        return;
    }
    if (state.selected_kind == ProjectFileKind::Image && state.preview_texture != nullptr) {
        draw_sprite_manager(state);
        return;
    }

    ImGui::Text("%s", state.selected_path.generic_string().c_str());
    ImGui::Text("Type: %s", ProjectBrowser::kind_name(state.selected_kind).data());
    const auto resolved = state.browser.resolve_for_external_open(state.selected_path);
    if (resolved.success && ImGui::Button("Open in default application")) {
        if (!SDL_OpenURL(file_url(resolved.path).c_str())) {
            state.status = "Could not open externally: " + std::string(SDL_GetError());
        }
    }
}

void draw_files(EditorState& state, SDL_Renderer* renderer, SDL_Window* window) {
    if (ImGui::BeginTable("workspace", 2,
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Project browser", ImGuiTableColumnFlags_WidthFixed, 390.0F);
        ImGui::TableSetupColumn("Editor", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextColumn();
        draw_file_list(state, renderer, window);
        ImGui::TableNextColumn();
        draw_selected_file(state, renderer);
        ImGui::EndTable();
    }
}

void draw_server_table(std::string_view id, std::span<const meat2d::net::ServerInfo> servers,
                       EditorState& state, bool public_listing) {
    if (!ImGui::BeginTable(id.data(), 5,
                           ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                               ImGuiTableFlags_Resizable)) {
        return;
    }
    ImGui::TableSetupColumn("Name");
    ImGui::TableSetupColumn("Mode / map");
    ImGui::TableSetupColumn("Players");
    ImGui::TableSetupColumn("Endpoint");
    ImGui::TableSetupColumn("Action");
    ImGui::TableHeadersRow();
    for (const auto& server : servers) {
        ImGui::PushID(static_cast<const void*>(&server));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(server.name.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%s / %s", server.mode.c_str(), server.map.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%d / %d", static_cast<int>(server.current_players),
                    static_cast<int>(server.maximum_clients));
        ImGui::TableNextColumn();
        const auto endpoint = server.endpoint.address + ':' + std::to_string(server.endpoint.port);
        ImGui::TextUnformatted(endpoint.c_str());
        ImGui::TableNextColumn();
        if (ImGui::SmallButton("Join")) {
            if (public_listing) {
                state.join_public(server.server_id);
            } else {
                state.join_direct(server.endpoint.address, server.endpoint.port);
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy")) {
            const auto text =
                public_listing ? "--directory " + state.directory_host + " --directory-port " +
                                     std::to_string(std::clamp(state.directory_port, 1, 65'535)) +
                                     " --server-id " + std::to_string(server.server_id)
                               : "--connect " + server.endpoint.address + " --port " +
                                     std::to_string(server.endpoint.port);
            ImGui::SetClipboardText(text.c_str());
            state.status = "Join information copied.";
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void draw_multiplayer(EditorState& state) {
    ImGui::SeparatorText("Developer session");
    ImGui::TextWrapped("Launch the bundled authoritative Elements Lab server and graphical client "
                       "without leaving the editor. These are editor-owned test processes and "
                       "close with the editor.");
    ImGui::SetNextItemWidth(300.0F);
    ImGui::InputText("Player name", &state.player_name);
    ImGui::SetNextItemWidth(300.0F);
    ImGui::InputText("Session name", &state.session_name);
    ImGui::SetNextItemWidth(120.0F);
    ImGui::InputInt("Game UDP port", &state.host_port);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0F);
    ImGui::InputInt("LAN discovery UDP port", &state.lan_port);
    ImGui::Checkbox("Advertise this host through the public directory", &state.advertise_public);
    if (state.hosted_server.running()) {
        if (ImGui::Button("Stop server", {125.0F, 0.0F})) {
            state.stop_host();
        }
    } else if (ImGui::Button("Start server", {125.0F, 0.0F})) {
        state.start_host();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!state.hosted_server.running());
    if (ImGui::Button("Join local host", {145.0F, 0.0F})) {
        state.join_direct("127.0.0.1", state.host_port);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%zu launched client(s)", state.launched_clients.size());
    if (!state.launched_clients.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Close launched clients")) {
            state.stop_clients();
        }
    }

    ImGui::SeparatorText("Direct join");
    ImGui::SetNextItemWidth(300.0F);
    ImGui::InputText("Host or IP", &state.direct_host);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0F);
    ImGui::InputInt("Port##direct", &state.direct_port);
    ImGui::SameLine();
    if (ImGui::Button("Join direct", {115.0F, 0.0F})) {
        state.join_direct(state.direct_host, state.direct_port);
    }

    ImGui::SeparatorText("LAN sessions");
    if (ImGui::Button("Refresh LAN")) {
        if (!state.lan_browser.refresh(
                static_cast<std::uint16_t>(std::clamp(state.lan_port, 1, 65'535)), 1)) {
            state.status = std::string(state.lan_browser.last_error());
        } else {
            state.status = "Searching LAN...";
        }
    }
    state.lan_browser.update();
    draw_server_table("lan-servers", state.lan_browser.servers(), state, false);

    ImGui::SeparatorText("Public directory");
    ImGui::InputText("Directory host", &state.directory_host);
    ImGui::SetNextItemWidth(120.0F);
    ImGui::InputInt("Directory UDP port", &state.directory_port);
    if (ImGui::Button("Refresh public list")) {
        if (!state.public_browser.refresh(
                {
                    .address = state.directory_host,
                    .port = static_cast<std::uint16_t>(std::clamp(state.directory_port, 1, 65'535)),
                },
                1)) {
            state.status = std::string(state.public_browser.last_error());
        } else {
            state.status = "Requesting public server list...";
        }
    }
    const bool public_was_searching = state.public_browser.searching();
    state.public_browser.update();
    if (public_was_searching && state.public_browser.complete()) {
        state.status =
            state.public_browser.last_error().empty()
                ? "Public list loaded: " + std::to_string(state.public_browser.servers().size()) +
                      " compatible server(s)."
                : std::string(state.public_browser.last_error());
    }
    if (state.public_browser.searching()) {
        ImGui::TextDisabled("Loading server pages...");
    }
    draw_server_table("public-servers", state.public_browser.servers(), state, true);
    ImGui::TextWrapped("Join launches the bundled living-lab client. Shipped games use Meat2D::Net "
                       "to present their own Host, LAN, Public, and Direct Join interface.");
}

void draw_editor(EditorState& state, SDL_Window* window, SDL_Renderer* renderer, bool& running) {
    poll_dialogs(state);
    state.poll_project_changes(renderer);
    state.poll_session_processes();
    const auto& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({0.0F, 0.0F});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Meat2D Editor", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar);
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("Project")) {
            if (ImGui::MenuItem("Close project", nullptr, false,
                                state.browser.is_open() && !state.dirty())) {
                state.close_selected();
                state.browser.close();
            }
            if (ImGui::MenuItem("Quit")) {
                if (state.dirty()) {
                    state.status = "Save or revert the current code or sprite settings before "
                                   "quitting.";
                } else {
                    running = false;
                }
            }
            ImGui::EndMenu();
        }
        ImGui::TextDisabled("Meat2D %s | %s", meat2d::version_string.data(),
                            state.browser.is_open()
                                ? state.browser.root().filename().string().c_str()
                                : "No project");
        ImGui::EndMenuBar();
    }

    if (!state.browser.is_open()) {
        draw_start(state, window);
    } else if (ImGui::BeginTabBar("main-tabs")) {
        if (ImGui::BeginTabItem("Project")) {
            draw_overview(state);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Code & Assets")) {
            draw_files(state, renderer, window);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Multiplayer")) {
            draw_multiplayer(state);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    const auto status_y = ImGui::GetWindowHeight() - ImGui::GetFrameHeightWithSpacing();
    ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), status_y));
    ImGui::Separator();
    ImGui::TextUnformatted(state.status.c_str());
    ImGui::End();
}

} // namespace

int main(int argc, char** argv) {
    bool smoke_test = false;
    bool process_smoke_test = false;
    std::filesystem::path initial_project;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--smoke-test") {
            smoke_test = true;
        } else if (argument == "--process-smoke-test") {
            smoke_test = true;
            process_smoke_test = true;
        } else {
            initial_project = argv[index];
        }
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer("Meat2D Engine Editor", 1440, 900,
                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                         (smoke_test ? SDL_WINDOW_HIDDEN : 0U),
                                     &window, &renderer)) {
        std::fprintf(stderr, "Editor window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    std::string ini_path;
    if (!smoke_test) {
        if (auto* preferences = SDL_GetPrefPath("Midwest Mystery Meat", "Meat2D Editor")) {
            ini_path = std::string(preferences) + "meat2d-editor.ini";
            SDL_free(preferences);
            io.IniFilename = ini_path.c_str();
        }
    } else {
        io.IniFilename = nullptr;
    }
    apply_theme();
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    EditorState state(argc > 0 ? argv[0] : "");
    if (!initial_project.empty()) {
        state.open_project(initial_project);
    }
    bool smoke_failed = false;
    if (smoke_test && state.browser.is_open()) {
        const auto image = std::find_if(
            state.browser.entries().begin(), state.browser.entries().end(),
            [](const ProjectEntry& entry) { return entry.kind == ProjectFileKind::Image; });
        if (image != state.browser.entries().end()) {
            state.select_file(*image, renderer);
            if (state.preview_texture == nullptr ||
                meat2d::assets::sprite_frame_count(
                    state.sprite_sheet, static_cast<std::uint32_t>(state.preview_width),
                    static_cast<std::uint32_t>(state.preview_height)) == 0U) {
                std::fprintf(stderr, "Editor image/sprite smoke failed: %s\n",
                             state.status.c_str());
                smoke_failed = true;
            }
        }
    }
    bool process_smoke_started = false;
    if (process_smoke_test) {
        process_smoke_started = state.start_host(true);
        if (!process_smoke_started) {
            std::fprintf(stderr, "Editor process smoke failed to start: %s\n",
                         state.status.c_str());
            smoke_failed = true;
        }
    }
    bool running = true;
    int rendered_frames = 0;
    const auto process_smoke_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                 event.window.windowID == SDL_GetWindowID(window))) {
                if (state.dirty()) {
                    state.status =
                        "Save or revert the current code or sprite settings before quitting.";
                } else {
                    running = false;
                }
            }
        }

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        draw_editor(state, window, renderer, running);
        ImGui::Render();

        SDL_SetRenderDrawColor(renderer, 7, 8, 12, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
        ++rendered_frames;
        if (process_smoke_test) {
            if (!process_smoke_started || !state.hosted_server.running()) {
                if (process_smoke_started && state.hosted_server.exit_code != 0) {
                    std::fprintf(stderr, "Editor process smoke server exited with %d\n",
                                 state.hosted_server.exit_code);
                    smoke_failed = true;
                }
                running = false;
            } else if (std::chrono::steady_clock::now() >= process_smoke_deadline) {
                std::fprintf(stderr, "Editor process smoke timed out\n");
                smoke_failed = true;
                running = false;
            }
        } else if (smoke_test && rendered_frames >= 3) {
            running = false;
        }
    }

    state.shutdown_session_processes();
    state.release_preview();
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return smoke_failed ? 1 : 0;
}
