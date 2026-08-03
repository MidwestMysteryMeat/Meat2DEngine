#include "meat2d/assets/Animation.hpp"

#include <algorithm>

namespace meat2d::assets {

SpriteAnimator::SpriteAnimator(std::uint16_t tick_rate) noexcept
    : tick_rate_(std::max<std::uint16_t>(1, tick_rate)) {}

void SpriteAnimator::set_sheet(const SpriteSheet* sheet) noexcept {
    sheet_ = sheet;
    reset();
}

const SpriteSheet* SpriteAnimator::sheet() const noexcept {
    return sheet_;
}

bool SpriteAnimator::play(std::string_view animation_name_value, bool restart) noexcept {
    if (sheet_ == nullptr) {
        return false;
    }
    const auto iterator = std::find_if(
        sheet_->animations.begin(), sheet_->animations.end(), [&](const SpriteAnimation& animation) {
            return animation.name == animation_name_value;
        });
    if (iterator == sheet_->animations.end()) {
        return false;
    }
    const auto index = static_cast<std::size_t>(iterator - sheet_->animations.begin());
    if (index != animation_index_ || restart) {
        animation_index_ = index;
        frame_offset_ = 0;
        frame_accumulator_ = 0;
        finished_ = false;
    }
    return true;
}

void SpriteAnimator::reset() noexcept {
    animation_index_ = 0;
    frame_offset_ = 0;
    frame_accumulator_ = 0;
    finished_ = false;
}

void SpriteAnimator::advance(std::uint32_t ticks) noexcept {
    const auto* current = animation();
    if (current == nullptr || finished_) {
        return;
    }
    frame_accumulator_ += static_cast<std::uint64_t>(ticks) * current->frames_per_second;
    while (frame_accumulator_ >= tick_rate_ && !finished_) {
        frame_accumulator_ -= tick_rate_;
        if (frame_offset_ + 1U < current->frame_count) {
            ++frame_offset_;
        } else if (current->loop) {
            frame_offset_ = 0;
        } else {
            frame_offset_ = current->frame_count - 1U;
            finished_ = true;
        }
    }
}

std::string_view SpriteAnimator::animation_name() const noexcept {
    const auto* current = animation();
    return current == nullptr ? std::string_view{} : current->name;
}

std::uint32_t SpriteAnimator::frame_index() const noexcept {
    const auto* current = animation();
    return current == nullptr ? 0U : current->first_frame + frame_offset_;
}

bool SpriteAnimator::finished() const noexcept {
    return finished_;
}

std::optional<SpriteFrame> SpriteAnimator::frame(std::uint32_t image_width,
                                                 std::uint32_t image_height) const noexcept {
    return sheet_ == nullptr ? std::nullopt
                             : sprite_frame(*sheet_, image_width, image_height, frame_index());
}

const SpriteAnimation* SpriteAnimator::animation() const noexcept {
    if (sheet_ == nullptr || animation_index_ >= sheet_->animations.size()) {
        return nullptr;
    }
    return &sheet_->animations[animation_index_];
}

} // namespace meat2d::assets
