#include "meat2d/assets/TextureAtlas.hpp"

#include <algorithm>
#include <bit>
#include <limits>

namespace meat2d::assets {
namespace {

constexpr std::uint64_t fnv_offset = 14695981039346656037ULL;
constexpr std::uint64_t fnv_prime = 1099511628211ULL;

void hash_byte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= value;
    hash *= fnv_prime;
}

void hash_u32(std::uint64_t& hash, std::uint32_t value) noexcept {
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        hash_byte(hash, static_cast<std::uint8_t>(value >> (index * 8U)));
    }
}

void hash_text(std::uint64_t& hash, std::string_view text) noexcept {
    hash_u32(hash, static_cast<std::uint32_t>(text.size()));
    for (const auto character : text) {
        hash_byte(hash, static_cast<std::uint8_t>(character));
    }
}

} // namespace

TextureAtlasCache::TextureAtlasCache(std::size_t maximum_entries)
    : maximum_entries_(std::clamp(maximum_entries, std::size_t{1}, maximum_texture_atlas_entries)) {
    entries_.reserve(maximum_entries_);
}

bool TextureAtlasCache::define(std::uint32_t asset_id, SpriteSheet sheet,
                               std::uint32_t image_width, std::uint32_t image_height) {
    if (asset_id == 0U || image_width == 0U || image_height == 0U ||
        image_width > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) ||
        image_height > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) ||
        !valid_sprite_sheet(sheet, image_width, image_height)) {
        return false;
    }
    const auto existing = std::find_if(entries_.begin(), entries_.end(), [asset_id](const auto& entry) {
        return entry.asset_id == asset_id;
    });
    if (existing != entries_.end()) {
        existing->sheet = std::move(sheet);
        existing->image_width = image_width;
        existing->image_height = image_height;
        return true;
    }
    if (entries_.size() == maximum_entries_) {
        return false;
    }
    entries_.push_back({asset_id, std::move(sheet), image_width, image_height});
    return true;
}

bool TextureAtlasCache::remove(std::uint32_t asset_id) {
    const auto iterator = std::find_if(entries_.begin(), entries_.end(), [asset_id](const auto& entry) {
        return entry.asset_id == asset_id;
    });
    if (iterator == entries_.end()) {
        return false;
    }
    entries_.erase(iterator);
    return true;
}

const SpriteSheet* TextureAtlasCache::sheet(std::uint32_t asset_id) const noexcept {
    const auto iterator = std::find_if(entries_.begin(), entries_.end(), [asset_id](const auto& entry) {
        return entry.asset_id == asset_id;
    });
    return iterator == entries_.end() ? nullptr : &iterator->sheet;
}

std::optional<AtlasRegion> TextureAtlasCache::resolve(std::uint32_t asset_id,
                                                       std::uint32_t frame) const {
    const auto iterator = std::find_if(entries_.begin(), entries_.end(), [asset_id](const auto& entry) {
        return entry.asset_id == asset_id;
    });
    if (iterator == entries_.end()) {
        return std::nullopt;
    }
    const auto resolved = sprite_frame(iterator->sheet, iterator->image_width,
                                       iterator->image_height, frame);
    if (!resolved || resolved->x > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) ||
        resolved->y > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) ||
        resolved->width > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()) ||
        resolved->height > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
        return std::nullopt;
    }
    return AtlasRegion{
        .asset_id = asset_id,
        .image = iterator->sheet.image,
        .source = {static_cast<std::int32_t>(resolved->x), static_cast<std::int32_t>(resolved->y),
                   static_cast<std::int32_t>(resolved->width),
                   static_cast<std::int32_t>(resolved->height)},
    };
}

std::uint64_t TextureAtlasCache::state_hash() const noexcept {
    std::vector<std::size_t> order(entries_.size());
    for (std::size_t index = 0; index < entries_.size(); ++index) {
        order[index] = index;
    }
    std::sort(order.begin(), order.end(), [this](std::size_t left, std::size_t right) {
        return entries_[left].asset_id < entries_[right].asset_id;
    });
    std::uint64_t hash = fnv_offset;
    hash_u32(hash, static_cast<std::uint32_t>(entries_.size()));
    for (const auto index : order) {
        const auto& entry = entries_[index];
        hash_u32(hash, entry.asset_id);
        hash_u32(hash, entry.image_width);
        hash_u32(hash, entry.image_height);
        hash_text(hash, encode_sprite_sheet_toml(entry.sheet));
    }
    return hash;
}

std::size_t TextureAtlasCache::size() const noexcept {
    return entries_.size();
}

std::size_t TextureAtlasCache::maximum_entries() const noexcept {
    return maximum_entries_;
}

} // namespace meat2d::assets
