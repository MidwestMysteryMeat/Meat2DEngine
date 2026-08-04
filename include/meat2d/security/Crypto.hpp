#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace meat2d::security {

inline constexpr std::uint16_t crypto_format_version = 1;
inline constexpr std::size_t aead_key_bytes = 32U;
inline constexpr std::size_t aead_nonce_bytes = 24U;
inline constexpr std::size_t aead_tag_bytes = 16U;
inline constexpr std::size_t default_max_plaintext_bytes = 64U * 1024U * 1024U;
inline constexpr std::size_t default_max_ciphertext_bytes =
    default_max_plaintext_bytes + aead_tag_bytes;
inline constexpr std::size_t default_max_aad_bytes = 4U * 1024U;
inline constexpr std::size_t default_max_working_bytes =
    128U * 1024U * 1024U + aead_tag_bytes;

using Key = std::array<std::uint8_t, aead_key_bytes>;
using Nonce = std::array<std::uint8_t, aead_nonce_bytes>;

struct CryptoLimits {
    std::size_t maximum_plaintext_bytes{default_max_plaintext_bytes};
    std::size_t maximum_ciphertext_bytes{default_max_ciphertext_bytes};
    std::size_t maximum_aad_bytes{default_max_aad_bytes};
    std::size_t maximum_working_bytes{default_max_working_bytes};
};

struct EncryptedBlock {
    std::uint16_t format_version{crypto_format_version};
    std::uint64_t key_id{};
    std::uint64_t plaintext_size{};
    Nonce nonce{};
    std::vector<std::uint8_t> ciphertext;
};

[[nodiscard]] std::optional<Key> generate_key() noexcept;

[[nodiscard]] std::optional<EncryptedBlock> encrypt_block(
    std::span<const std::uint8_t> plaintext,
    std::span<const std::uint8_t> associated_data,
    const Key& key,
    std::uint64_t key_id = 0,
    CryptoLimits limits = {});

[[nodiscard]] std::optional<std::vector<std::uint8_t>> decrypt_block(
    const EncryptedBlock& block,
    std::span<const std::uint8_t> associated_data,
    const Key& key,
    CryptoLimits limits = {});

[[nodiscard]] std::optional<std::vector<std::uint8_t>> encode_block(
    const EncryptedBlock& block,
    CryptoLimits limits = {});

[[nodiscard]] std::optional<EncryptedBlock> decode_block(
    std::span<const std::uint8_t> encoded,
    CryptoLimits limits = {});

void secure_zero(std::span<std::uint8_t> bytes) noexcept;

} // namespace meat2d::security
