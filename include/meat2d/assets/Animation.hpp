#pragma once

#include "meat2d/assets/SpriteSheet.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace meat2d::assets {

// Fixed-tick sprite animation playback. The animator stores no floating-point
// time, so identical tick inputs produce identical frame sequences.
class SpriteAnimator {
  public:
    explicit SpriteAnimator(std::uint16_t tick_rate = 60) noexcept;

    void set_sheet(const SpriteSheet* sheet) noexcept;
    [[nodiscard]] const SpriteSheet* sheet() const noexcept;

    bool play(std::string_view animation_name, bool restart = true) noexcept;
    void reset() noexcept;
    void advance(std::uint32_t ticks = 1) noexcept;

    [[nodiscard]] std::string_view animation_name() const noexcept;
    [[nodiscard]] std::uint32_t frame_index() const noexcept;
    [[nodiscard]] bool finished() const noexcept;
    [[nodiscard]] std::optional<SpriteFrame> frame(std::uint32_t image_width,
                                                   std::uint32_t image_height) const noexcept;

  private:
    [[nodiscard]] const SpriteAnimation* animation() const noexcept;

    const SpriteSheet* sheet_{};
    std::size_t animation_index_{};
    std::uint32_t frame_offset_{};
    std::uint64_t frame_accumulator_{};
    std::uint16_t tick_rate_{};
    bool finished_{};
};

} // namespace meat2d::assets
