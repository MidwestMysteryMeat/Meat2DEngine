#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace meat2d::compression {

inline constexpr std::uint16_t compression_format_version = 1;
inline constexpr std::size_t default_max_uncompressed_bytes = 64U * 1024U * 1024U;
inline constexpr std::size_t default_max_compressed_bytes = 64U * 1024U * 1024U;
inline constexpr std::size_t default_max_working_bytes = 128U * 1024U * 1024U;

enum class CodecId : std::uint8_t {
    None = 0,
    Lzav = 1
};

struct CompressionLimits {
    std::size_t maximum_uncompressed_bytes{default_max_uncompressed_bytes};
    std::size_t maximum_compressed_bytes{default_max_compressed_bytes};
    std::size_t maximum_working_bytes{default_max_working_bytes};
};

struct CompressedBlock {
    CodecId codec{CodecId::None};
    std::uint16_t format_version{compression_format_version};
    std::uint64_t uncompressed_size{};
    std::uint64_t checksum{};
    std::vector<std::uint8_t> bytes;
};

[[nodiscard]] std::optional<CompressedBlock> compress_block(
    std::span<const std::uint8_t> input,
    CodecId preferred = CodecId::Lzav,
    CompressionLimits limits = {});

[[nodiscard]] std::optional<std::vector<std::uint8_t>> decompress_block(
    const CompressedBlock& block,
    CompressionLimits limits = {});

[[nodiscard]] std::optional<std::vector<std::uint8_t>> encode_block(
    const CompressedBlock& block,
    CompressionLimits limits = {});

[[nodiscard]] std::optional<CompressedBlock> decode_block(
    std::span<const std::uint8_t> encoded,
    CompressionLimits limits = {});

} // namespace meat2d::compression
