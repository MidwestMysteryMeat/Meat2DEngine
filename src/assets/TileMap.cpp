#include "meat2d/assets/TileMap.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <type_traits>
#include <utility>

namespace meat2d::assets {
namespace {

constexpr std::uint64_t fnv_offset = 14695981039346656037ULL;
constexpr std::uint64_t fnv_prime = 1099511628211ULL;
constexpr std::int32_t maximum_dimension = 8192;
constexpr std::uint64_t maximum_cells = 16ULL * 1024ULL * 1024ULL;
constexpr std::uint16_t maximum_layers = 64;
constexpr std::uint32_t maximum_definitions = 65535U;
constexpr std::uint32_t maximum_text_bytes = 1024U * 1024U;

void hash_byte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= value;
    hash *= fnv_prime;
}

template <typename Integer>
void hash_integer(std::uint64_t& hash, Integer value) noexcept {
    using Unsigned = std::make_unsigned_t<Integer>;
    const auto converted = static_cast<Unsigned>(value);
    for (std::size_t index = 0; index < sizeof(Integer); ++index) {
        hash_byte(hash, static_cast<std::uint8_t>(converted >> (index * 8U)));
    }
}

void hash_text(std::uint64_t& hash, std::string_view text) noexcept {
    hash_integer(hash, static_cast<std::uint32_t>(text.size()));
    for (const auto character : text) {
        hash_byte(hash, static_cast<std::uint8_t>(character));
    }
}

void hash_rect(std::uint64_t& hash, RectI rect) noexcept {
    hash_integer(hash, rect.x);
    hash_integer(hash, rect.y);
    hash_integer(hash, rect.width);
    hash_integer(hash, rect.height);
}

void append_u8(std::vector<std::uint8_t>& bytes, std::uint8_t value) {
    bytes.push_back(value);
}

void append_u16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void append_i16(std::vector<std::uint8_t>& bytes, std::int16_t value) {
    append_u16(bytes, std::bit_cast<std::uint16_t>(value));
}

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (std::size_t offset = 0; offset < sizeof(value); ++offset) {
        bytes.push_back(static_cast<std::uint8_t>(value >> (offset * 8U)));
    }
}

void append_i32(std::vector<std::uint8_t>& bytes, std::int32_t value) {
    append_u32(bytes, std::bit_cast<std::uint32_t>(value));
}

void append_text(std::vector<std::uint8_t>& bytes, std::string_view text) {
    append_u32(bytes, static_cast<std::uint32_t>(text.size()));
    bytes.insert(bytes.end(), text.begin(), text.end());
}

void append_rect(std::vector<std::uint8_t>& bytes, RectI rect) {
    append_i32(bytes, rect.x);
    append_i32(bytes, rect.y);
    append_i32(bytes, rect.width);
    append_i32(bytes, rect.height);
}

class Reader {
  public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] bool read_u8(std::uint8_t& value) noexcept {
        if (remaining() < 1U) {
            return false;
        }
        value = bytes_[offset_++];
        return true;
    }

    [[nodiscard]] bool read_u16(std::uint16_t& value) noexcept {
        if (remaining() < 2U) {
            return false;
        }
        value = static_cast<std::uint16_t>(bytes_[offset_]) |
                static_cast<std::uint16_t>(bytes_[offset_ + 1U] << 8U);
        offset_ += 2U;
        return true;
    }

    [[nodiscard]] bool read_i16(std::int16_t& value) noexcept {
        std::uint16_t encoded{};
        if (!read_u16(encoded)) {
            return false;
        }
        value = std::bit_cast<std::int16_t>(encoded);
        return true;
    }

    [[nodiscard]] bool read_u32(std::uint32_t& value) noexcept {
        if (remaining() < 4U) {
            return false;
        }
        value = static_cast<std::uint32_t>(bytes_[offset_]) |
                (static_cast<std::uint32_t>(bytes_[offset_ + 1U]) << 8U) |
                (static_cast<std::uint32_t>(bytes_[offset_ + 2U]) << 16U) |
                (static_cast<std::uint32_t>(bytes_[offset_ + 3U]) << 24U);
        offset_ += 4U;
        return true;
    }

    [[nodiscard]] bool read_i32(std::int32_t& value) noexcept {
        std::uint32_t encoded{};
        if (!read_u32(encoded)) {
            return false;
        }
        value = std::bit_cast<std::int32_t>(encoded);
        return true;
    }

    [[nodiscard]] bool read_text(std::string& value) {
        std::uint32_t length{};
        if (!read_u32(length) || length > maximum_text_bytes || remaining() < length) {
            return false;
        }
        value.assign(reinterpret_cast<const char*>(bytes_.data() + offset_), length);
        offset_ += length;
        return true;
    }

    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - offset_;
    }

  private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_{};
};

bool read_rect(Reader& reader, RectI& rect) noexcept {
    return reader.read_i32(rect.x) && reader.read_i32(rect.y) &&
           reader.read_i32(rect.width) && reader.read_i32(rect.height);
}

} // namespace

TileMap::TileMap(std::int32_t width, std::int32_t height, std::uint16_t layer_count) {
    resize(width, height, layer_count);
}

bool TileMap::valid_dimensions(std::int32_t width, std::int32_t height,
                               std::uint16_t layer_count) const noexcept {
    if (width < 0 || height < 0 || width > maximum_dimension || height > maximum_dimension ||
        layer_count == 0U || layer_count > maximum_layers) {
        return false;
    }
    return static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) <=
           maximum_cells;
}

bool TileMap::resize(std::int32_t width, std::int32_t height, std::uint16_t layer_count) {
    if (!valid_dimensions(width, height, layer_count)) {
        return false;
    }
    width_ = width;
    height_ = height;
    layers_.clear();
    layers_.reserve(layer_count);
    const auto cell_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (std::uint16_t index = 0; index < layer_count; ++index) {
        layers_.push_back(Layer{
            .info = TileLayerInfo{.name = "Layer" + std::to_string(index),
                                  .z = static_cast<std::int16_t>(index)},
            .tiles = std::vector<TileId>(cell_count, empty_tile),
        });
    }
    return true;
}

std::int32_t TileMap::width() const noexcept {
    return width_;
}

std::int32_t TileMap::height() const noexcept {
    return height_;
}

std::uint16_t TileMap::layer_count() const noexcept {
    return static_cast<std::uint16_t>(layers_.size());
}

bool TileMap::in_bounds(Vec2i cell) const noexcept {
    return cell.x >= 0 && cell.y >= 0 && cell.x < width_ && cell.y < height_;
}

bool TileMap::add_layer(std::string name, std::int16_t z, bool visible) {
    if (layers_.size() >= maximum_layers || name.size() > maximum_text_bytes) {
        return false;
    }
    const auto cell_count = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
    layers_.push_back(Layer{
        .info = TileLayerInfo{.name = std::move(name), .z = z, .visible = visible},
        .tiles = std::vector<TileId>(cell_count, empty_tile),
    });
    return true;
}

const TileLayerInfo* TileMap::layer_info(std::uint16_t layer) const noexcept {
    return layer < layers_.size() ? &layers_[layer].info : nullptr;
}

std::span<const TileId> TileMap::layer_tiles(std::uint16_t layer) const noexcept {
    return layer < layers_.size() ? std::span<const TileId>(layers_[layer].tiles) :
                                    std::span<const TileId>{};
}

bool TileMap::define_tile(TileDefinition definition) {
    if (definition.id == empty_tile) {
        return false;
    }
    const auto iterator = std::lower_bound(
        definitions_.begin(), definitions_.end(), definition.id,
        [](const TileDefinition& value, TileId id) { return value.id < id; });
    if (iterator != definitions_.end() && iterator->id == definition.id) {
        *iterator = definition;
    } else {
        definitions_.insert(iterator, definition);
    }
    return true;
}

const TileDefinition* TileMap::tile_definition(TileId id) const noexcept {
    const auto iterator = std::lower_bound(
        definitions_.begin(), definitions_.end(), id,
        [](const TileDefinition& value, TileId value_id) { return value.id < value_id; });
    return iterator != definitions_.end() && iterator->id == id ? &*iterator : nullptr;
}

std::size_t TileMap::offset(Vec2i cell) const noexcept {
    return static_cast<std::size_t>(cell.y) * static_cast<std::size_t>(width_) +
           static_cast<std::size_t>(cell.x);
}

bool TileMap::set_tile(std::uint16_t layer, Vec2i cell, TileId id) noexcept {
    if (layer >= layers_.size() || !in_bounds(cell) ||
        (id != empty_tile && tile_definition(id) == nullptr)) {
        return false;
    }
    layers_[layer].tiles[offset(cell)] = id;
    return true;
}

TileId TileMap::tile(std::uint16_t layer, Vec2i cell) const noexcept {
    return layer < layers_.size() && in_bounds(cell) ? layers_[layer].tiles[offset(cell)]
                                                     : empty_tile;
}

std::vector<RectI> TileMap::solid_cells(std::uint16_t layer, RectI area) const {
    std::vector<RectI> result;
    if (layer >= layers_.size() || area.empty() || width_ == 0 || height_ == 0) {
        return result;
    }
    const auto area_right = static_cast<std::int64_t>(area.x) + area.width;
    const auto area_bottom = static_cast<std::int64_t>(area.y) + area.height;
    const auto start_x = std::max<std::int64_t>(0, area.x);
    const auto start_y = std::max<std::int64_t>(0, area.y);
    const auto end_x = std::min<std::int64_t>(width_, area_right);
    const auto end_y = std::min<std::int64_t>(height_, area_bottom);
    if (start_x >= end_x || start_y >= end_y) {
        return result;
    }
    for (std::int64_t y = start_y; y < end_y; ++y) {
        for (std::int64_t x = start_x; x < end_x; ++x) {
            const Vec2i cell{static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)};
            const auto id = tile(layer, cell);
            const auto* definition = tile_definition(id);
            if (definition != nullptr && definition->solid) {
                result.push_back({cell.x, cell.y, 1, 1});
            }
        }
    }
    return result;
}

bool TileMap::valid_tile_references() const noexcept {
    for (const auto& layer : layers_) {
        if (layer.tiles.size() != static_cast<std::size_t>(width_) *
                                      static_cast<std::size_t>(height_)) {
            return false;
        }
        for (const auto id : layer.tiles) {
            if (id != empty_tile && tile_definition(id) == nullptr) {
                return false;
            }
        }
    }
    return true;
}

std::uint64_t TileMap::state_hash() const noexcept {
    std::uint64_t hash = fnv_offset;
    hash_integer(hash, width_);
    hash_integer(hash, height_);
    hash_integer(hash, static_cast<std::uint32_t>(definitions_.size()));
    for (const auto& definition : definitions_) {
        hash_integer(hash, definition.id);
        hash_rect(hash, definition.atlas_source);
        hash_byte(hash, static_cast<std::uint8_t>(definition.solid));
        hash_integer(hash, definition.category_bits);
        hash_integer(hash, definition.mask_bits);
    }
    hash_integer(hash, static_cast<std::uint16_t>(layers_.size()));
    for (const auto& layer : layers_) {
        hash_text(hash, layer.info.name);
        hash_integer(hash, layer.info.z);
        hash_byte(hash, static_cast<std::uint8_t>(layer.info.visible));
        for (const auto id : layer.tiles) {
            hash_integer(hash, id);
        }
    }
    return hash;
}

std::vector<std::uint8_t> TileMap::serialize() const {
    if (!valid_dimensions(width_, height_, layer_count()) ||
        definitions_.size() > maximum_definitions || !valid_tile_references()) {
        return {};
    }
    for (const auto& layer : layers_) {
        if (layer.info.name.size() > maximum_text_bytes) {
            return {};
        }
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(32U + definitions_.size() * 32U +
                  layers_.size() * (16U + layer_tiles(0).size() * sizeof(TileId)));
    append_u8(bytes, 'M');
    append_u8(bytes, '2');
    append_u8(bytes, 'T');
    append_u8(bytes, 'M');
    append_u16(bytes, tile_map_format_version);
    append_i32(bytes, width_);
    append_i32(bytes, height_);
    append_u16(bytes, layer_count());
    append_u32(bytes, static_cast<std::uint32_t>(definitions_.size()));
    for (const auto& definition : definitions_) {
        append_u16(bytes, definition.id);
        append_rect(bytes, definition.atlas_source);
        append_u8(bytes, static_cast<std::uint8_t>(definition.solid));
        append_u16(bytes, definition.category_bits);
        append_u16(bytes, definition.mask_bits);
    }
    for (const auto& layer : layers_) {
        append_text(bytes, layer.info.name);
        append_i16(bytes, layer.info.z);
        append_u8(bytes, static_cast<std::uint8_t>(layer.info.visible));
        append_u32(bytes, static_cast<std::uint32_t>(layer.tiles.size()));
        for (const auto id : layer.tiles) {
            append_u16(bytes, id);
        }
    }
    return bytes;
}

std::optional<TileMap> TileMap::deserialize(std::span<const std::uint8_t> bytes) {
    Reader reader(bytes);
    std::uint8_t magic[4]{};
    for (auto& value : magic) {
        if (!reader.read_u8(value)) {
            return std::nullopt;
        }
    }
    if (magic[0] != 'M' || magic[1] != '2' || magic[2] != 'T' || magic[3] != 'M') {
        return std::nullopt;
    }
    std::uint16_t version{};
    std::int32_t width{};
    std::int32_t height{};
    std::uint16_t layer_count{};
    std::uint32_t definition_count{};
    if (!reader.read_u16(version) || version != tile_map_format_version ||
        !reader.read_i32(width) || !reader.read_i32(height) || !reader.read_u16(layer_count) ||
        !reader.read_u32(definition_count) || definition_count > maximum_definitions) {
        return std::nullopt;
    }
    TileMap result;
    if (!result.valid_dimensions(width, height, layer_count)) {
        return std::nullopt;
    }
    result.width_ = width;
    result.height_ = height;
    result.layers_.clear();
    result.definitions_.reserve(definition_count);
    TileId previous_id{};
    for (std::uint32_t index = 0; index < definition_count; ++index) {
        TileDefinition definition;
        std::uint8_t solid{};
        if (!reader.read_u16(definition.id) || definition.id == empty_tile ||
            definition.id <= previous_id || !read_rect(reader, definition.atlas_source) ||
            !reader.read_u8(solid) || solid > 1U || !reader.read_u16(definition.category_bits) ||
            !reader.read_u16(definition.mask_bits)) {
            return std::nullopt;
        }
        definition.solid = solid != 0U;
        result.definitions_.push_back(definition);
        previous_id = definition.id;
    }
    const auto cell_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    result.layers_.reserve(layer_count);
    for (std::uint16_t index = 0; index < layer_count; ++index) {
        Layer layer;
        std::uint8_t visible{};
        std::uint32_t tile_count{};
        if (!reader.read_text(layer.info.name) || !reader.read_i16(layer.info.z) ||
            !reader.read_u8(visible) || visible > 1U || !reader.read_u32(tile_count) ||
            tile_count != cell_count || reader.remaining() <
                                           static_cast<std::size_t>(tile_count) * sizeof(TileId)) {
            return std::nullopt;
        }
        layer.info.visible = visible != 0U;
        layer.tiles.resize(cell_count);
        for (auto& id : layer.tiles) {
            if (!reader.read_u16(id)) {
                return std::nullopt;
            }
        }
        result.layers_.push_back(std::move(layer));
    }
    if (reader.remaining() != 0U) {
        return std::nullopt;
    }
    if (!result.valid_tile_references()) {
        return std::nullopt;
    }
    return result;
}

} // namespace meat2d::assets
