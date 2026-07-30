#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace meat2d::assets {

inline constexpr std::uint32_t sprite_sheet_format_version = 1;
inline constexpr std::size_t maximum_sprite_animations = 256;
inline constexpr std::size_t maximum_sprite_animation_name_bytes = 48;

struct SpriteAnimation {
    std::string name{"idle"};
    std::uint32_t first_frame{};
    std::uint32_t frame_count{1};
    std::uint16_t frames_per_second{8};
    bool loop{true};

    friend bool operator==(const SpriteAnimation&, const SpriteAnimation&) = default;
};

struct SpriteSheet {
    std::string image;
    std::uint16_t frame_width{16};
    std::uint16_t frame_height{16};
    std::uint16_t margin{};
    std::uint16_t spacing{};
    std::vector<SpriteAnimation> animations;

    friend bool operator==(const SpriteSheet&, const SpriteSheet&) = default;
};

struct SpriteFrame {
    std::uint32_t index{};
    std::uint32_t x{};
    std::uint32_t y{};
    std::uint32_t width{};
    std::uint32_t height{};
};

struct SpriteSheetParseResult {
    std::optional<SpriteSheet> sheet;
    std::string error;
    std::size_t error_line{};
};

[[nodiscard]] std::uint32_t sprite_frame_count(const SpriteSheet& sheet, std::uint32_t image_width,
                                               std::uint32_t image_height) noexcept;
[[nodiscard]] std::optional<SpriteFrame> sprite_frame(const SpriteSheet& sheet,
                                                      std::uint32_t image_width,
                                                      std::uint32_t image_height,
                                                      std::uint32_t index) noexcept;
[[nodiscard]] bool valid_sprite_sheet(const SpriteSheet& sheet, std::uint32_t image_width = 0,
                                      std::uint32_t image_height = 0) noexcept;
[[nodiscard]] std::string encode_sprite_sheet_toml(const SpriteSheet& sheet);
[[nodiscard]] SpriteSheetParseResult decode_sprite_sheet_toml(std::string_view text);

} // namespace meat2d::assets
