#include "meat2d/tools/AssetPack.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

namespace meat2d::tools {
namespace {

constexpr std::array<std::uint8_t, 4> pack_magic{'M', '2', 'P', 'K'};
constexpr std::array<std::uint8_t, 8> aad_magic{'M', '2', 'P', 'K', '-', 'A', 'A', 'D'};
constexpr std::size_t pack_header_bytes = 12U;
constexpr std::size_t entry_header_bytes = 32U;
constexpr std::uint8_t encrypted_flag = 1U;

bool valid_limits(AssetPackLimits limits) noexcept {
    return limits.maximum_entries != 0U && limits.maximum_path_bytes != 0U &&
           limits.maximum_entry_bytes != 0U && limits.maximum_pack_bytes >= pack_header_bytes;
}

bool safe_add(std::size_t first, std::size_t second, std::size_t maximum) noexcept {
    return first <= maximum && second <= maximum - first;
}

void append_u16(std::vector<std::uint8_t>& output, std::uint16_t value) {
    output.push_back(static_cast<std::uint8_t>(value));
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void append_u32(std::vector<std::uint8_t>& output, std::uint32_t value) {
    for (std::size_t shift = 0; shift < 32U; shift += 8U) {
        output.push_back(static_cast<std::uint8_t>(value >> shift));
    }
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

    bool read_u32(std::uint32_t& value) noexcept {
        value = 0U;
        for (std::size_t shift = 0; shift < 32U; shift += 8U) {
            std::uint8_t byte{};
            if (!read_u8(byte)) {
                return false;
            }
            value |= static_cast<std::uint32_t>(byte) << shift;
        }
        return true;
    }

    bool read_u64(std::uint64_t& value) noexcept {
        value = 0U;
        for (std::size_t shift = 0; shift < 64U; shift += 8U) {
            std::uint8_t byte{};
            if (!read_u8(byte)) {
                return false;
            }
            value |= static_cast<std::uint64_t>(byte) << shift;
        }
        return true;
    }

    [[nodiscard]] std::size_t offset() const noexcept { return offset_; }
    [[nodiscard]] std::size_t remaining() const noexcept { return bytes_.size() - offset_; }
    [[nodiscard]] std::span<const std::uint8_t> rest() const noexcept {
        return bytes_.subspan(offset_);
    }

  private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_{};
};

std::optional<std::string> normalize_path(std::string_view path,
                                          std::size_t maximum_bytes) {
    if (path.empty() || path.size() > maximum_bytes) {
        return std::nullopt;
    }
    std::string normalized(path);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    if (normalized.empty() || normalized.front() == '/' || normalized.find('\0') != std::string::npos ||
        normalized.find(':') != std::string::npos) {
        return std::nullopt;
    }

    std::string clean;
    std::size_t component_start = 0U;
    while (component_start < normalized.size()) {
        const auto separator = normalized.find('/', component_start);
        const auto component_end = separator == std::string::npos ? normalized.size() : separator;
        const auto component = std::string_view(normalized).substr(
            component_start, component_end - component_start);
        if (component.empty() || component == "." || component == "..") {
            return std::nullopt;
        }
        if (!clean.empty()) {
            clean.push_back('/');
        }
        clean.append(component);
        if (separator == std::string::npos) {
            break;
        }
        component_start = separator + 1U;
    }
    return clean.empty() || clean.size() > maximum_bytes ? std::nullopt
                                                          : std::optional(std::move(clean));
}

std::vector<std::uint8_t> make_aad(std::string_view path,
                                   std::uint64_t key_id,
                                   std::size_t source_size) {
    std::vector<std::uint8_t> aad;
    aad.reserve(aad_magic.size() + 4U + path.size() + 8U + 8U);
    aad.insert(aad.end(), aad_magic.begin(), aad_magic.end());
    append_u32(aad, static_cast<std::uint32_t>(path.size()));
    aad.insert(aad.end(), path.begin(), path.end());
    append_u64(aad, key_id);
    append_u64(aad, source_size);
    return aad;
}

const AssetPackEntry* find_entry(const AssetPack& pack, std::string_view path) noexcept {
    const auto iterator = std::lower_bound(
        pack.entries.begin(), pack.entries.end(), path,
        [](const AssetPackEntry& entry, std::string_view value) { return entry.path < value; });
    return iterator != pack.entries.end() && iterator->path == path ? &*iterator : nullptr;
}

} // namespace

std::optional<std::vector<std::uint8_t>> build_asset_pack(
    std::span<const AssetPackInput> inputs,
    AssetPackBuildOptions options) {
    if (!valid_limits(options.pack_limits) ||
        inputs.size() > options.pack_limits.maximum_entries) {
        return std::nullopt;
    }
    try {
        struct PreparedInput {
            std::string path;
            std::span<const std::uint8_t> bytes;
            bool encrypt{};
        };
        std::vector<PreparedInput> prepared;
        prepared.reserve(inputs.size());
        for (const auto& input : inputs) {
            const auto normalized = normalize_path(input.path, options.pack_limits.maximum_path_bytes);
            if (!normalized || input.bytes.size() > options.pack_limits.maximum_entry_bytes) {
                return std::nullopt;
            }
            prepared.push_back({.path = *normalized, .bytes = input.bytes, .encrypt = input.encrypt});
        }
        std::sort(prepared.begin(), prepared.end(),
                  [](const PreparedInput& left, const PreparedInput& right) {
                      return left.path < right.path;
                  });
        if (std::adjacent_find(prepared.begin(), prepared.end(),
                               [](const PreparedInput& left, const PreparedInput& right) {
                                   return left.path == right.path;
                               }) != prepared.end()) {
            return std::nullopt;
        }

        std::vector<std::vector<std::uint8_t>> payloads;
        payloads.reserve(prepared.size());
        std::vector<std::uint64_t> key_ids(prepared.size(), 0U);
        std::vector<std::size_t> source_sizes;
        source_sizes.reserve(prepared.size());
        for (const auto& input : prepared) {
            const auto compressed = compression::compress_block(
                input.bytes, compression::CodecId::Lzav, options.compression_limits);
            if (!compressed) {
                return std::nullopt;
            }
            const auto encoded_compressed = compression::encode_block(
                *compressed, options.compression_limits);
            if (!encoded_compressed) {
                return std::nullopt;
            }
            std::vector<std::uint8_t> payload = *encoded_compressed;
            if (input.encrypt) {
                if (options.encryption_key == nullptr) {
                    return std::nullopt;
                }
                const auto aad = make_aad(input.path, options.encryption_key_id, input.bytes.size());
                const auto encrypted = security::encrypt_block(
                    payload, aad, *options.encryption_key, options.encryption_key_id,
                    options.crypto_limits);
                if (!encrypted) {
                    return std::nullopt;
                }
                const auto encoded_encrypted = security::encode_block(
                    *encrypted, options.crypto_limits);
                if (!encoded_encrypted) {
                    return std::nullopt;
                }
                payload = *encoded_encrypted;
                key_ids[&input - prepared.data()] = options.encryption_key_id;
            }
            if (payload.size() > options.pack_limits.maximum_entry_bytes) {
                return std::nullopt;
            }
            payloads.push_back(std::move(payload));
            source_sizes.push_back(input.bytes.size());
        }

        std::vector<std::uint8_t> output;
        output.reserve(pack_header_bytes);
        output.insert(output.end(), pack_magic.begin(), pack_magic.end());
        append_u16(output, asset_pack_format_version);
        append_u16(output, 0U);
        append_u32(output, static_cast<std::uint32_t>(prepared.size()));
        for (std::size_t index = 0; index < prepared.size(); ++index) {
            const auto& input = prepared[index];
            const auto& payload = payloads[index];
            if (input.path.size() > std::numeric_limits<std::uint32_t>::max() ||
                !safe_add(output.size(), entry_header_bytes, options.pack_limits.maximum_pack_bytes) ||
                !safe_add(output.size() + entry_header_bytes, input.path.size(),
                          options.pack_limits.maximum_pack_bytes) ||
                !safe_add(output.size() + entry_header_bytes + input.path.size(), payload.size(),
                          options.pack_limits.maximum_pack_bytes)) {
                return std::nullopt;
            }
            append_u32(output, static_cast<std::uint32_t>(input.path.size()));
            output.push_back(input.encrypt ? encrypted_flag : 0U);
            output.push_back(0U);
            output.push_back(0U);
            output.push_back(0U);
            append_u64(output, key_ids[index]);
            append_u64(output, payload.size());
            append_u64(output, source_sizes[index]);
            output.insert(output.end(), input.path.begin(), input.path.end());
            output.insert(output.end(), payload.begin(), payload.end());
        }
        return output;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<AssetPack> open_asset_pack(std::span<const std::uint8_t> encoded,
                                         AssetPackLimits limits) {
    if (!valid_limits(limits) || encoded.size() > limits.maximum_pack_bytes ||
        encoded.size() < pack_header_bytes) {
        return std::nullopt;
    }
    try {
        Reader reader(encoded);
        for (const auto expected : pack_magic) {
            std::uint8_t value{};
            if (!reader.read_u8(value) || value != expected) {
                return std::nullopt;
            }
        }
        std::uint16_t version{};
        std::uint16_t flags{};
        std::uint32_t entry_count{};
        if (!reader.read_u16(version) || !reader.read_u16(flags) ||
            !reader.read_u32(entry_count) || version != asset_pack_format_version || flags != 0U ||
            entry_count > limits.maximum_entries) {
            return std::nullopt;
        }

        AssetPack pack{};
        pack.bytes.assign(encoded.begin(), encoded.end());
        pack.entries.reserve(entry_count);
        std::size_t reader_base = 0U;
        for (std::uint32_t index = 0; index < entry_count; ++index) {
            std::uint32_t path_size{};
            std::uint8_t entry_flags{};
            std::uint8_t reserved[3]{};
            std::uint64_t key_id{};
            std::uint64_t payload_size{};
            std::uint64_t source_size{};
            if (!reader.read_u32(path_size) || !reader.read_u8(entry_flags) ||
                !reader.read_u8(reserved[0]) || !reader.read_u8(reserved[1]) ||
                !reader.read_u8(reserved[2]) || !reader.read_u64(key_id) ||
                !reader.read_u64(payload_size) || !reader.read_u64(source_size) ||
                path_size == 0U || path_size > limits.maximum_path_bytes ||
                payload_size > limits.maximum_entry_bytes || source_size > limits.maximum_entry_bytes ||
                (entry_flags & static_cast<std::uint8_t>(~encrypted_flag)) != 0U ||
                reserved[0] != 0U || reserved[1] != 0U || reserved[2] != 0U ||
                payload_size > reader.remaining() ||
                source_size > std::numeric_limits<std::size_t>::max()) {
                return std::nullopt;
            }
            if (path_size > reader.remaining()) {
                return std::nullopt;
            }
            const auto path_begin = reader_base + reader.offset();
            const auto path_bytes = encoded.subspan(path_begin, path_size);
            reader = Reader(encoded.subspan(path_begin + path_size));
            const auto normalized = normalize_path(
                std::string_view(reinterpret_cast<const char*>(path_bytes.data()), path_bytes.size()),
                limits.maximum_path_bytes);
            if (!normalized || *normalized != std::string(
                                    reinterpret_cast<const char*>(path_bytes.data()), path_bytes.size()) ||
                payload_size > reader.remaining()) {
                return std::nullopt;
            }
            const auto payload_offset = path_begin + path_size;
            pack.entries.push_back({
                .path = *normalized,
                .payload_offset = payload_offset,
                .payload_size = static_cast<std::size_t>(payload_size),
                .source_size = static_cast<std::size_t>(source_size),
                .key_id = key_id,
                .encrypted = (entry_flags & encrypted_flag) != 0U,
            });
            reader_base = payload_offset + static_cast<std::size_t>(payload_size);
            reader = Reader(encoded.subspan(reader_base));
        }
        if (!std::is_sorted(pack.entries.begin(), pack.entries.end(),
                            [](const AssetPackEntry& left, const AssetPackEntry& right) {
                                return left.path < right.path;
                            }) ||
            std::adjacent_find(pack.entries.begin(), pack.entries.end(),
                               [](const AssetPackEntry& left, const AssetPackEntry& right) {
                                   return left.path == right.path;
                               }) != pack.entries.end() ||
            reader.remaining() != 0U) {
            return std::nullopt;
        }
        return pack;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::vector<std::uint8_t>> read_asset_pack_entry(
    const AssetPack& pack,
    std::string_view path,
    const security::Key* encryption_key,
    AssetPackLimits pack_limits,
    compression::CompressionLimits compression_limits,
    security::CryptoLimits crypto_limits) {
    if (!valid_limits(pack_limits) || pack.bytes.size() > pack_limits.maximum_pack_bytes) {
        return std::nullopt;
    }
    const auto normalized = normalize_path(path, pack_limits.maximum_path_bytes);
    const auto* entry = normalized ? find_entry(pack, *normalized) : nullptr;
    if (entry == nullptr || entry->payload_offset > pack.bytes.size() ||
        entry->payload_size > pack.bytes.size() - entry->payload_offset) {
        return std::nullopt;
    }
    try {
        const auto payload = std::span<const std::uint8_t>(
            pack.bytes.data() + entry->payload_offset, entry->payload_size);
        std::vector<std::uint8_t> compressed_bytes(payload.begin(), payload.end());
        if (entry->encrypted) {
            if (encryption_key == nullptr) {
                return std::nullopt;
            }
            const auto encrypted = security::decode_block(payload, crypto_limits);
            if (!encrypted || encrypted->key_id != entry->key_id) {
                return std::nullopt;
            }
            const auto aad = make_aad(entry->path, entry->key_id, entry->source_size);
            const auto decrypted = security::decrypt_block(
                *encrypted, aad, *encryption_key, crypto_limits);
            if (!decrypted) {
                return std::nullopt;
            }
            compressed_bytes = *decrypted;
        }
        const auto compressed = compression::decode_block(compressed_bytes, compression_limits);
        if (!compressed || compressed->uncompressed_size != entry->source_size) {
            return std::nullopt;
        }
        return compression::decompress_block(*compressed, compression_limits);
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace meat2d::tools
