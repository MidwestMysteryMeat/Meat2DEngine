#include "meat2d/compression/Compression.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

#if defined(MEAT2D_HAS_LZAV)
#define LZAV_NS_CUSTOM meat2d_lzav
#include <lzav.h>
#undef LZAV_NS_CUSTOM
#endif

namespace meat2d::compression {
namespace {

constexpr std::array<std::uint8_t, 4> block_magic{'M', '2', 'C', 'P'};
constexpr std::size_t block_header_bytes = 32U;
constexpr std::uint64_t fnv_offset = 14695981039346656037ULL;
constexpr std::uint64_t fnv_prime = 1099511628211ULL;

bool valid_limits(CompressionLimits limits) noexcept {
    return limits.maximum_uncompressed_bytes != 0U &&
           limits.maximum_compressed_bytes != 0U && limits.maximum_working_bytes != 0U;
}

bool valid_codec(CodecId codec) noexcept {
    return codec == CodecId::None || codec == CodecId::Lzav;
}

bool fits_working_budget(std::size_t first,
                         std::size_t second,
                         std::size_t maximum) noexcept {
    return first <= maximum && second <= maximum - first;
}

std::uint64_t checksum(std::span<const std::uint8_t> bytes) noexcept {
    auto hash = fnv_offset;
    for (const auto value : bytes) {
        hash ^= value;
        hash *= fnv_prime;
    }
    return hash;
}

void append_u16(std::vector<std::uint8_t>& output, std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value));
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void append_u64(std::vector<std::uint8_t>& output, std::uint64_t value) {
    for (std::size_t shift = 0; shift < 64U; shift += 8U) {
        output.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

class Reader {
  public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    bool read_u8(std::uint8_t& value) noexcept {
        if (remaining() < 1U) {
            return false;
        }
        value = bytes_[offset_++];
        return true;
    }

    bool read_u16(std::uint16_t& value) noexcept {
        std::uint8_t low{};
        std::uint8_t high{};
        if (!read_u8(low) || !read_u8(high)) {
            return false;
        }
        value = static_cast<std::uint16_t>(low) |
                static_cast<std::uint16_t>(static_cast<std::uint16_t>(high) << 8U);
        return true;
    }

    bool read_u64(std::uint64_t& value) noexcept {
        value = 0;
        for (std::size_t shift = 0; shift < 64U; shift += 8U) {
            std::uint8_t byte{};
            if (!read_u8(byte)) {
                return false;
            }
            value |= static_cast<std::uint64_t>(byte) << shift;
        }
        return true;
    }

    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - offset_;
    }

    [[nodiscard]] std::span<const std::uint8_t> rest() const noexcept {
        return bytes_.subspan(offset_);
    }

  private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_{};
};

std::optional<CompressedBlock> make_raw(std::span<const std::uint8_t> input,
                                        CompressionLimits limits) {
    if (input.size() > limits.maximum_uncompressed_bytes ||
        input.size() > limits.maximum_compressed_bytes ||
        !fits_working_budget(input.size(), input.size(), limits.maximum_working_bytes)) {
        return std::nullopt;
    }
    CompressedBlock block{};
    block.codec = CodecId::None;
    block.uncompressed_size = input.size();
    block.checksum = checksum(input);
    block.bytes.assign(input.begin(), input.end());
    return block;
}

} // namespace

std::optional<CompressedBlock> compress_block(std::span<const std::uint8_t> input,
                                              CodecId preferred,
                                              CompressionLimits limits) {
    if (!valid_limits(limits) || !valid_codec(preferred) ||
        input.size() > limits.maximum_uncompressed_bytes) {
        return std::nullopt;
    }
    try {
        if (preferred == CodecId::None || input.empty()) {
            return make_raw(input, limits);
        }

#if defined(MEAT2D_HAS_LZAV)
        if (input.size() <= static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            const auto source_size = static_cast<int>(input.size());
            const auto bound = meat2d_lzav::lzav_compress_bound(source_size);
            if (bound > 0 && fits_working_budget(
                                  input.size(), static_cast<std::size_t>(bound),
                                  limits.maximum_working_bytes)) {
                std::vector<std::uint8_t> compressed(static_cast<std::size_t>(bound));
                const auto compressed_size = meat2d_lzav::lzav_compress_default(
                    input.data(), compressed.data(), source_size, bound);
                if (compressed_size > 0 &&
                    static_cast<std::size_t>(compressed_size) < input.size() &&
                    static_cast<std::size_t>(compressed_size) <= limits.maximum_compressed_bytes) {
                    compressed.resize(static_cast<std::size_t>(compressed_size));
                    return CompressedBlock{
                        .codec = CodecId::Lzav,
                        .format_version = compression_format_version,
                        .uncompressed_size = input.size(),
                        .checksum = checksum(input),
                        .bytes = std::move(compressed),
                    };
                }
            }
        }
#endif
        return make_raw(input, limits);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::vector<std::uint8_t>> decompress_block(const CompressedBlock& block,
                                                          CompressionLimits limits) {
    if (!valid_limits(limits) || block.format_version != compression_format_version ||
        !valid_codec(block.codec) || block.uncompressed_size > limits.maximum_uncompressed_bytes ||
        block.bytes.size() > limits.maximum_compressed_bytes ||
        block.uncompressed_size > std::numeric_limits<std::size_t>::max()) {
        return std::nullopt;
    }
    try {
        const auto output_size = static_cast<std::size_t>(block.uncompressed_size);
        if (!fits_working_budget(
                block.bytes.size(), output_size, limits.maximum_working_bytes)) {
            return std::nullopt;
        }
        if (block.codec == CodecId::None) {
            if (block.bytes.size() != output_size) {
                return std::nullopt;
            }
        }
        std::vector<std::uint8_t> output(output_size);
        if (block.codec == CodecId::None) {
            output = block.bytes;
        } else {
#if defined(MEAT2D_HAS_LZAV)
            if (block.bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
                output_size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
                return std::nullopt;
            }
            const auto decoded = meat2d_lzav::lzav_decompress(
                block.bytes.data(), output.data(), static_cast<int>(block.bytes.size()),
                static_cast<int>(output_size));
            if (decoded < 0 || static_cast<std::size_t>(decoded) != output_size) {
                return std::nullopt;
            }
#else
            return std::nullopt;
#endif
        }
        return checksum(output) == block.checksum ? std::optional(std::move(output))
                                                  : std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::vector<std::uint8_t>> encode_block(const CompressedBlock& block,
                                                      CompressionLimits limits) {
    if (!valid_limits(limits) || block.format_version != compression_format_version ||
        !valid_codec(block.codec) || block.bytes.size() > limits.maximum_compressed_bytes ||
        block.uncompressed_size > limits.maximum_uncompressed_bytes ||
        block.uncompressed_size > std::numeric_limits<std::size_t>::max()) {
        return std::nullopt;
    }
    if (block.codec == CodecId::None && block.bytes.size() != block.uncompressed_size) {
        return std::nullopt;
    }
    try {
        if (block.bytes.size() > std::numeric_limits<std::uint64_t>::max() - block_header_bytes) {
            return std::nullopt;
        }
        std::vector<std::uint8_t> encoded;
        encoded.reserve(block_header_bytes + block.bytes.size());
        encoded.insert(encoded.end(), block_magic.begin(), block_magic.end());
        append_u16(encoded, block.format_version);
        encoded.push_back(static_cast<std::uint8_t>(block.codec));
        encoded.push_back(0U);
        append_u64(encoded, block.uncompressed_size);
        append_u64(encoded, block.bytes.size());
        append_u64(encoded, block.checksum);
        encoded.insert(encoded.end(), block.bytes.begin(), block.bytes.end());
        return encoded;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<CompressedBlock> decode_block(std::span<const std::uint8_t> encoded,
                                            CompressionLimits limits) {
    if (!valid_limits(limits) || encoded.size() < block_header_bytes ||
        encoded.size() - block_header_bytes > limits.maximum_compressed_bytes) {
        return std::nullopt;
    }
    try {
        Reader reader(encoded);
        for (const auto expected : block_magic) {
            std::uint8_t value{};
            if (!reader.read_u8(value) || value != expected) {
                return std::nullopt;
            }
        }
        CompressedBlock block{};
        std::uint8_t codec{};
        std::uint8_t flags{};
        std::uint64_t compressed_size{};
        if (!reader.read_u16(block.format_version) || !reader.read_u8(codec) ||
            !reader.read_u8(flags) || !reader.read_u64(block.uncompressed_size) ||
            !reader.read_u64(compressed_size) || !reader.read_u64(block.checksum) ||
            flags != 0U || block.format_version != compression_format_version ||
            codec > static_cast<std::uint8_t>(CodecId::Lzav) ||
            block.uncompressed_size > limits.maximum_uncompressed_bytes ||
            compressed_size > limits.maximum_compressed_bytes ||
            compressed_size != reader.remaining()) {
            return std::nullopt;
        }
        block.codec = static_cast<CodecId>(codec);
        const auto bytes = reader.rest();
        block.bytes.assign(bytes.begin(), bytes.end());
        return block;
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace meat2d::compression
