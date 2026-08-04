#include "meat2d/compression/Compression.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct Sample {
    std::string_view name;
    std::vector<std::uint8_t> bytes;
};

struct Measurement {
    meat2d::compression::CodecId codec{};
    std::size_t compressed_bytes{};
    double compression_ms{};
    double decompression_ms{};
    std::uint64_t digest{};
};

std::uint64_t digest(std::span<const std::uint8_t> bytes) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto value : bytes) {
        hash ^= value;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::vector<std::uint8_t> make_text(std::size_t repetitions, std::string_view prefix) {
    std::string text;
    text.reserve(repetitions * 96U);
    for (std::size_t index = 0; index < repetitions; ++index) {
        text += prefix;
        text += " entity=";
        text += std::to_string(index % 128U);
        text += " position=(";
        text += std::to_string((index * 17U) % 2048U);
        text += ",";
        text += std::to_string((index * 31U) % 1024U);
        text += ") flags=visible,collidable,replicated\n";
    }
    return {text.begin(), text.end()};
}

std::vector<std::uint8_t> make_tile_map() {
    std::vector<std::uint8_t> bytes(256U * 256U * 2U);
    for (std::size_t tile = 0; tile < 256U * 256U; ++tile) {
        const auto value = static_cast<std::uint16_t>(
            ((tile / 256U) > 220U ? 3U : 1U) + ((tile % 17U) == 0U ? 4U : 0U));
        bytes[tile * 2U] = static_cast<std::uint8_t>(value);
        bytes[tile * 2U + 1U] = static_cast<std::uint8_t>(value >> 8U);
    }
    return bytes;
}

std::vector<std::uint8_t> make_structured_bytes(std::size_t size) {
    std::vector<std::uint8_t> bytes(size);
    for (std::size_t index = 0; index < size; ++index) {
        bytes[index] = static_cast<std::uint8_t>(
            ((index / 64U) * 13U + (index % 7U) * 3U) & 0xFFU);
    }
    return bytes;
}

std::vector<std::uint8_t> make_incompressible_bytes(std::size_t size) {
    std::vector<std::uint8_t> bytes(size);
    std::uint32_t state = 0xC001D00DU;
    for (auto& value : bytes) {
        state ^= state << 13U;
        state ^= state >> 17U;
        state ^= state << 5U;
        value = static_cast<std::uint8_t>(state >> 24U);
    }
    return bytes;
}

std::string_view codec_name(meat2d::compression::CodecId codec) {
    return codec == meat2d::compression::CodecId::Lzav ? "lzav" : "raw";
}

bool measure(const Sample& sample,
             meat2d::compression::CodecId preferred,
             std::size_t repetitions,
             Measurement& result) {
    const auto compression_start = std::chrono::steady_clock::now();
    std::optional<meat2d::compression::CompressedBlock> compressed;
    for (std::size_t iteration = 0; iteration < repetitions; ++iteration) {
        compressed = meat2d::compression::compress_block(sample.bytes, preferred);
        if (!compressed) {
            return false;
        }
    }
    const auto compression_end = std::chrono::steady_clock::now();

    const auto decompression_start = std::chrono::steady_clock::now();
    std::optional<std::vector<std::uint8_t>> restored;
    for (std::size_t iteration = 0; iteration < repetitions; ++iteration) {
        restored = meat2d::compression::decompress_block(*compressed);
        if (!restored || *restored != sample.bytes) {
            return false;
        }
    }
    const auto decompression_end = std::chrono::steady_clock::now();

    result.codec = compressed->codec;
    result.compressed_bytes = compressed->bytes.size();
    result.compression_ms = std::chrono::duration<double, std::milli>(
                                compression_end - compression_start)
                                .count() /
                            static_cast<double>(repetitions);
    result.decompression_ms = std::chrono::duration<double, std::milli>(
                                  decompression_end - decompression_start)
                                  .count() /
                              static_cast<double>(repetitions);
    result.digest = digest(*restored);
    return true;
}

} // namespace

int main() {
    const std::vector<Sample> samples{
        {"scene", make_text(512U, "node type=Sprite2D name=crate")},
        {"tile-map", make_tile_map()},
        {"json-toml", make_text(768U, "[entity.properties] key=movement_speed")},
        {"world-chunk", make_structured_bytes(256U * 1024U)},
        {"snapshot", make_structured_bytes(384U * 1024U)},
        {"incompressible", make_incompressible_bytes(256U * 1024U)},
    };
    constexpr std::size_t repetitions = 5U;

    std::cout << std::fixed << std::setprecision(3);
    for (const auto& sample : samples) {
        for (const auto preferred : {meat2d::compression::CodecId::None,
                                     meat2d::compression::CodecId::Lzav}) {
            Measurement result{};
            if (!measure(sample, preferred, repetitions, result)) {
                std::cerr << "compression benchmark failed for " << sample.name << '\n';
                return 1;
            }
            const auto ratio = static_cast<double>(result.compressed_bytes) /
                               static_cast<double>(sample.bytes.size());
            std::cout << sample.name << " preferred=" << codec_name(preferred)
                      << " selected=" << codec_name(result.codec)
                      << " input_bytes=" << sample.bytes.size()
                      << " stored_bytes=" << result.compressed_bytes << " ratio=" << ratio
                      << " compress_ms=" << result.compression_ms
                      << " decompress_ms=" << result.decompression_ms << " digest=0x"
                      << std::hex << result.digest << std::dec << '\n';
        }
    }
    return 0;
}
