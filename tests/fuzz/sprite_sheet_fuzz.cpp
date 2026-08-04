#include "meat2d/assets/SpriteSheet.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const auto result = meat2d::assets::decode_sprite_sheet_toml(
        std::string_view(reinterpret_cast<const char*>(data), size));
    if (result.sheet) {
        const auto& sheet = *result.sheet;
        if (meat2d::assets::valid_sprite_sheet(sheet, 4096U, 4096U)) {
            const auto count = meat2d::assets::sprite_frame_count(sheet, 4096U, 4096U);
            if (count != 0U) {
                static_cast<void>(meat2d::assets::sprite_frame(sheet, 4096U, 4096U, count - 1U));
            }
        }
    }
    return 0;
}

