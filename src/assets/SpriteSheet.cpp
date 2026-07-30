#include "meat2d/assets/SpriteSheet.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <limits>
#include <sstream>

namespace meat2d::assets {
namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

std::size_t find_comment(std::string_view text) {
    bool quoted = false;
    bool escaped = false;
    for (std::size_t index = 0; index < text.size(); ++index) {
        const auto character = text[index];
        if (escaped) {
            escaped = false;
        } else if (quoted && character == '\\') {
            escaped = true;
        } else if (character == '"') {
            quoted = !quoted;
        } else if (!quoted && character == '#') {
            return index;
        }
    }
    return std::string_view::npos;
}

bool parse_unsigned(std::string_view text, std::uint32_t& value) {
    text = trim(text);
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return !text.empty() && result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool parse_bool(std::string_view text, bool& value) {
    text = trim(text);
    if (text == "true") {
        value = true;
        return true;
    }
    if (text == "false") {
        value = false;
        return true;
    }
    return false;
}

std::optional<std::string> parse_string(std::string_view text) {
    text = trim(text);
    if (text.size() < 2U || text.front() != '"' || text.back() != '"') {
        return std::nullopt;
    }
    text.remove_prefix(1);
    text.remove_suffix(1);
    std::string result;
    result.reserve(text.size());
    bool escaped = false;
    for (const auto character : text) {
        if (escaped) {
            if (character == '"' || character == '\\') {
                result.push_back(character);
            } else if (character == 'n') {
                result.push_back('\n');
            } else {
                return std::nullopt;
            }
            escaped = false;
        } else if (character == '\\') {
            escaped = true;
        } else {
            result.push_back(character);
        }
    }
    return escaped ? std::nullopt : std::optional(std::move(result));
}

std::string quote(std::string_view text) {
    std::string result{"\""};
    for (const auto character : text) {
        if (character == '"' || character == '\\') {
            result.push_back('\\');
            result.push_back(character);
        } else if (character == '\n') {
            result += "\\n";
        } else {
            result.push_back(character);
        }
    }
    result.push_back('"');
    return result;
}

bool safe_relative_image(std::string_view image) {
    if (image.empty() || image.size() > 512U) {
        return false;
    }
    const std::filesystem::path path(image);
    return !path.is_absolute() && !path.has_root_name() && !path.has_root_directory() &&
           path.has_filename() &&
           std::none_of(path.begin(), path.end(),
                        [](const std::filesystem::path& component) { return component == ".."; });
}

bool valid_animation(const SpriteAnimation& animation, std::uint32_t available_frames) {
    if (animation.name.empty() || animation.name.size() > maximum_sprite_animation_name_bytes ||
        animation.frame_count == 0U || animation.frames_per_second == 0U ||
        animation.first_frame > std::numeric_limits<std::uint32_t>::max() - animation.frame_count) {
        return false;
    }
    return available_frames == 0U ||
           animation.first_frame + animation.frame_count <= available_frames;
}

SpriteSheetParseResult parse_error(std::size_t line, std::string message) {
    return {
        .sheet = std::nullopt,
        .error = std::move(message),
        .error_line = line,
    };
}

} // namespace

std::uint32_t sprite_frame_count(const SpriteSheet& sheet, std::uint32_t image_width,
                                 std::uint32_t image_height) noexcept {
    if (sheet.frame_width == 0U || sheet.frame_height == 0U ||
        image_width < static_cast<std::uint32_t>(sheet.margin) * 2U ||
        image_height < static_cast<std::uint32_t>(sheet.margin) * 2U) {
        return 0;
    }
    const auto usable_width = image_width - static_cast<std::uint32_t>(sheet.margin) * 2U;
    const auto usable_height = image_height - static_cast<std::uint32_t>(sheet.margin) * 2U;
    if (usable_width < sheet.frame_width || usable_height < sheet.frame_height ||
        usable_width > std::numeric_limits<std::uint32_t>::max() - sheet.spacing ||
        usable_height > std::numeric_limits<std::uint32_t>::max() - sheet.spacing) {
        return 0;
    }
    const auto column_stride = static_cast<std::uint32_t>(sheet.frame_width) + sheet.spacing;
    const auto row_stride = static_cast<std::uint32_t>(sheet.frame_height) + sheet.spacing;
    const auto columns = (usable_width + sheet.spacing) / column_stride;
    const auto rows = (usable_height + sheet.spacing) / row_stride;
    if (columns == 0U || rows == 0U || rows > std::numeric_limits<std::uint32_t>::max() / columns) {
        return 0;
    }
    return columns * rows;
}

std::optional<SpriteFrame> sprite_frame(const SpriteSheet& sheet, std::uint32_t image_width,
                                        std::uint32_t image_height, std::uint32_t index) noexcept {
    const auto count = sprite_frame_count(sheet, image_width, image_height);
    if (index >= count) {
        return std::nullopt;
    }
    const auto usable_width = image_width - static_cast<std::uint32_t>(sheet.margin) * 2U;
    const auto columns = (usable_width + sheet.spacing) /
                         (static_cast<std::uint32_t>(sheet.frame_width) + sheet.spacing);
    const auto column = index % columns;
    const auto row = index / columns;
    return SpriteFrame{
        .index = index,
        .x = static_cast<std::uint32_t>(sheet.margin) +
             column * (static_cast<std::uint32_t>(sheet.frame_width) + sheet.spacing),
        .y = static_cast<std::uint32_t>(sheet.margin) +
             row * (static_cast<std::uint32_t>(sheet.frame_height) + sheet.spacing),
        .width = sheet.frame_width,
        .height = sheet.frame_height,
    };
}

bool valid_sprite_sheet(const SpriteSheet& sheet, std::uint32_t image_width,
                        std::uint32_t image_height) noexcept {
    if (!safe_relative_image(sheet.image) || sheet.frame_width == 0U || sheet.frame_height == 0U ||
        sheet.animations.size() > maximum_sprite_animations) {
        return false;
    }
    const auto frames = image_width == 0U || image_height == 0U
                            ? 0U
                            : sprite_frame_count(sheet, image_width, image_height);
    if ((image_width != 0U || image_height != 0U) && frames == 0U) {
        return false;
    }
    return std::all_of(
        sheet.animations.begin(), sheet.animations.end(),
        [&](const SpriteAnimation& animation) { return valid_animation(animation, frames); });
}

std::string encode_sprite_sheet_toml(const SpriteSheet& sheet) {
    if (!valid_sprite_sheet(sheet)) {
        return {};
    }
    std::ostringstream output;
    output << "# Meat2D sprite sheet\n"
           << "version = " << sprite_sheet_format_version << '\n'
           << "image = " << quote(sheet.image) << '\n'
           << "frame_width = " << sheet.frame_width << '\n'
           << "frame_height = " << sheet.frame_height << '\n'
           << "margin = " << sheet.margin << '\n'
           << "spacing = " << sheet.spacing << '\n';
    for (const auto& animation : sheet.animations) {
        output << "\n[[animation]]\n"
               << "name = " << quote(animation.name) << '\n'
               << "first_frame = " << animation.first_frame << '\n'
               << "frame_count = " << animation.frame_count << '\n'
               << "frames_per_second = " << animation.frames_per_second << '\n'
               << "loop = " << (animation.loop ? "true" : "false") << '\n';
    }
    return output.str();
}

SpriteSheetParseResult decode_sprite_sheet_toml(std::string_view text) {
    SpriteSheet sheet{};
    std::optional<std::size_t> animation_index;
    bool saw_version = false;
    bool saw_image = false;
    bool saw_frame_width = false;
    bool saw_frame_height = false;
    std::size_t line_number = 0;
    while (!text.empty()) {
        ++line_number;
        const auto newline = text.find('\n');
        auto line = trim(text.substr(0, newline));
        text = newline == std::string_view::npos ? std::string_view{} : text.substr(newline + 1U);
        if (const auto comment = find_comment(line); comment != std::string_view::npos) {
            line = trim(line.substr(0, comment));
        }
        if (line.empty()) {
            continue;
        }
        if (line == "[[animation]]") {
            if (sheet.animations.size() >= maximum_sprite_animations) {
                return parse_error(line_number, "too many animations");
            }
            sheet.animations.emplace_back();
            animation_index = sheet.animations.size() - 1U;
            continue;
        }
        const auto separator = line.find('=');
        if (separator == std::string_view::npos) {
            return parse_error(line_number, "expected key = value");
        }
        const auto key = trim(line.substr(0, separator));
        const auto value = trim(line.substr(separator + 1U));
        if (animation_index) {
            auto& animation = sheet.animations[*animation_index];
            if (key == "name") {
                const auto parsed = parse_string(value);
                if (!parsed) {
                    return parse_error(line_number, "invalid animation name");
                }
                animation.name = *parsed;
            } else if (key == "first_frame") {
                if (!parse_unsigned(value, animation.first_frame)) {
                    return parse_error(line_number, "invalid first_frame");
                }
            } else if (key == "frame_count") {
                if (!parse_unsigned(value, animation.frame_count)) {
                    return parse_error(line_number, "invalid frame_count");
                }
            } else if (key == "frames_per_second") {
                std::uint32_t parsed = 0;
                if (!parse_unsigned(value, parsed) ||
                    parsed > std::numeric_limits<std::uint16_t>::max()) {
                    return parse_error(line_number, "invalid frames_per_second");
                }
                animation.frames_per_second = static_cast<std::uint16_t>(parsed);
            } else if (key == "loop") {
                if (!parse_bool(value, animation.loop)) {
                    return parse_error(line_number, "invalid loop value");
                }
            } else {
                return parse_error(line_number, "unknown animation field");
            }
            continue;
        }

        if (key == "version") {
            std::uint32_t version = 0;
            if (!parse_unsigned(value, version) || version != sprite_sheet_format_version) {
                return parse_error(line_number, "unsupported sprite version");
            }
            saw_version = true;
        } else if (key == "image") {
            const auto parsed = parse_string(value);
            if (!parsed) {
                return parse_error(line_number, "invalid image path");
            }
            sheet.image = *parsed;
            saw_image = true;
        } else if (key == "frame_width" || key == "frame_height" || key == "margin" ||
                   key == "spacing") {
            std::uint32_t parsed = 0;
            if (!parse_unsigned(value, parsed) ||
                parsed > std::numeric_limits<std::uint16_t>::max()) {
                return parse_error(line_number, "invalid sprite dimension");
            }
            const auto converted = static_cast<std::uint16_t>(parsed);
            if (key == "frame_width") {
                sheet.frame_width = converted;
                saw_frame_width = true;
            } else if (key == "frame_height") {
                sheet.frame_height = converted;
                saw_frame_height = true;
            } else if (key == "margin") {
                sheet.margin = converted;
            } else {
                sheet.spacing = converted;
            }
        } else {
            return parse_error(line_number, "unknown sprite-sheet field");
        }
    }

    if (!saw_version || !saw_image || !saw_frame_width || !saw_frame_height ||
        !valid_sprite_sheet(sheet)) {
        return parse_error(line_number, "sprite sheet is missing required or valid fields");
    }
    return {
        .sheet = std::move(sheet),
        .error = {},
        .error_line = 0,
    };
}

} // namespace meat2d::assets
