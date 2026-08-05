#pragma once

#include "meat2d/compression/Compression.hpp"
#include "meat2d/security/Crypto.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace meat2d::tools {

inline constexpr std::uint16_t asset_pack_format_version = 1;
inline constexpr std::size_t default_max_pack_entries = 10'000U;
inline constexpr std::size_t default_max_pack_path_bytes = 4096U;
inline constexpr std::size_t default_max_pack_entry_bytes = 64U * 1024U * 1024U;
inline constexpr std::size_t default_max_pack_bytes = 512U * 1024U * 1024U;

struct AssetPackInput {
    std::string_view path;
    std::span<const std::uint8_t> bytes;
    bool encrypt{false};
};

struct AssetPackLimits {
    std::size_t maximum_entries{default_max_pack_entries};
    std::size_t maximum_path_bytes{default_max_pack_path_bytes};
    std::size_t maximum_entry_bytes{default_max_pack_entry_bytes};
    std::size_t maximum_pack_bytes{default_max_pack_bytes};
};

struct AssetPackBuildOptions {
    AssetPackLimits pack_limits{};
    compression::CompressionLimits compression_limits{};
    security::CryptoLimits crypto_limits{};
    const security::Key* encryption_key{};
    std::uint64_t encryption_key_id{};
};

struct AssetPackEntry {
    std::string path;
    std::size_t payload_offset{};
    std::size_t payload_size{};
    std::size_t source_size{};
    std::uint64_t key_id{};
    bool encrypted{};
};

struct AssetPack {
    std::vector<std::uint8_t> bytes;
    std::vector<AssetPackEntry> entries;
};

[[nodiscard]] std::optional<std::vector<std::uint8_t>> build_asset_pack(
    std::span<const AssetPackInput> inputs,
    AssetPackBuildOptions options = {});

[[nodiscard]] std::optional<AssetPack> open_asset_pack(
    std::span<const std::uint8_t> encoded,
    AssetPackLimits limits = {});

[[nodiscard]] std::optional<std::vector<std::uint8_t>> read_asset_pack_entry(
    const AssetPack& pack,
    std::string_view path,
    const security::Key* encryption_key = nullptr,
    AssetPackLimits pack_limits = {},
    compression::CompressionLimits compression_limits = {},
    security::CryptoLimits crypto_limits = {});

} // namespace meat2d::tools
