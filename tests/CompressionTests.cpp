#include "TestSupport.hpp"

#include "meat2d/compression/Compression.hpp"

#include <cstdint>
#include <vector>

namespace meat2d_tests {

void test_compression_blocks() {
    std::vector<std::uint8_t> source(64U * 1024U);
    for (std::size_t index = 0; index < source.size(); ++index) {
        source[index] = static_cast<std::uint8_t>((index / 32U) % 4U);
    }

    const auto compressed = meat2d::compression::compress_block(source);
    check(compressed.has_value(), "compression rejected a bounded source block");
    if (!compressed) {
        return;
    }
#if defined(MEAT2D_TEST_HAS_LZAV)
    check(compressed->codec == meat2d::compression::CodecId::Lzav,
          "LZAV did not win for a repetitive source block");
#else
    check(compressed->codec == meat2d::compression::CodecId::None,
          "raw fallback was not selected when LZAV is disabled");
#endif
    const auto encoded = meat2d::compression::encode_block(*compressed);
    check(encoded.has_value(), "compression envelope could not be encoded");
    if (!encoded) {
        return;
    }
    const auto decoded = meat2d::compression::decode_block(*encoded);
    check(decoded.has_value(), "compression envelope could not be decoded");
    if (decoded) {
        const auto restored = meat2d::compression::decompress_block(*decoded);
        check(restored && *restored == source,
              "compressed block did not round-trip its source bytes");
    }
    if (!decoded) {
        return;
    }

    const auto raw = meat2d::compression::compress_block(
        source, meat2d::compression::CodecId::None);
    check(raw && raw->codec == meat2d::compression::CodecId::None,
          "raw compression fallback did not preserve its codec ID");

    auto corrupted = *encoded;
    corrupted.back() ^= 0x80U;
    const auto corrupted_block = meat2d::compression::decode_block(corrupted);
    check(corrupted_block && !meat2d::compression::decompress_block(*corrupted_block),
          "corrupted compressed data bypassed checksum validation");
    corrupted.pop_back();
    check(!meat2d::compression::decode_block(corrupted),
          "truncated compression data was accepted");

    auto oversized = *encoded;
    for (std::size_t index = 8; index < 16U; ++index) {
        oversized[index] = 0xFFU;
    }
    check(!meat2d::compression::decode_block(oversized),
          "compression envelope accepted an oversized decompression target");

    const meat2d::compression::CompressionLimits tiny_limits{
        .maximum_uncompressed_bytes = 32,
        .maximum_compressed_bytes = 32,
        .maximum_working_bytes = 64,
    };
    check(!meat2d::compression::compress_block(source, meat2d::compression::CodecId::Lzav,
                                               tiny_limits),
          "compression ignored its input size budget");

    const meat2d::compression::CompressionLimits tiny_working_limits{
        .maximum_uncompressed_bytes = source.size(),
        .maximum_compressed_bytes = source.size(),
        .maximum_working_bytes = 1024,
    };
    check(!meat2d::compression::decompress_block(*decoded, tiny_working_limits),
          "decompression ignored its working-memory budget");
}

} // namespace meat2d_tests
