#include "meat2d/security/Crypto.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

#if defined(MEAT2D_HAS_SODIUM)
#include <sodium.h>
#endif

namespace meat2d::security {
namespace {

constexpr std::array<std::uint8_t, 4> block_magic{'M', '2', 'A', 'E'};
constexpr std::size_t block_header_bytes = 56U;

bool valid_limits(CryptoLimits limits) noexcept {
    return limits.maximum_plaintext_bytes != 0U &&
           limits.maximum_ciphertext_bytes >= aead_tag_bytes &&
           limits.maximum_aad_bytes != 0U && limits.maximum_working_bytes != 0U;
}

bool fits_working_budget(std::size_t first,
                         std::size_t second,
                         std::size_t maximum) noexcept {
    return first <= maximum && second <= maximum - first;
}

bool fits_working_budget(std::size_t first,
                         std::size_t second,
                         std::size_t third,
                         std::size_t maximum) noexcept {
    if (first > maximum) {
        return false;
    }
    maximum -= first;
    if (second > maximum) {
        return false;
    }
    return third <= maximum - second;
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

    bool read_bytes(std::span<std::uint8_t> output) noexcept {
        if (remaining() < output.size()) {
            return false;
        }
        std::copy_n(bytes_.data() + offset_, output.size(), output.data());
        offset_ += output.size();
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

bool valid_block(const EncryptedBlock& block, CryptoLimits limits) noexcept {
    return valid_limits(limits) && block.format_version == crypto_format_version &&
           block.plaintext_size <= limits.maximum_plaintext_bytes &&
           block.plaintext_size <= std::numeric_limits<std::size_t>::max() &&
           block.ciphertext.size() >= aead_tag_bytes &&
           block.ciphertext.size() <= limits.maximum_ciphertext_bytes &&
           block.ciphertext.size() - aead_tag_bytes ==
               static_cast<std::size_t>(block.plaintext_size) &&
           fits_working_budget(block.ciphertext.size(),
                               static_cast<std::size_t>(block.plaintext_size),
                               limits.maximum_working_bytes);
}

bool sodium_ready() noexcept {
#if defined(MEAT2D_HAS_SODIUM)
    return sodium_init() >= 0;
#else
    return false;
#endif
}

} // namespace

void secure_zero(std::span<std::uint8_t> bytes) noexcept {
#if defined(MEAT2D_HAS_SODIUM)
    if (!bytes.empty()) {
        sodium_memzero(bytes.data(), bytes.size());
    }
#else
    volatile std::uint8_t* destination = bytes.data();
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        destination[index] = 0U;
    }
#endif
}

std::optional<Key> generate_key() noexcept {
    if (!sodium_ready()) {
        return std::nullopt;
    }
    Key key{};
#if defined(MEAT2D_HAS_SODIUM)
    crypto_aead_xchacha20poly1305_ietf_keygen(key.data());
#endif
    return key;
}

std::optional<EncryptedBlock> encrypt_block(std::span<const std::uint8_t> plaintext,
                                            std::span<const std::uint8_t> associated_data,
                                            const Key& key,
                                            std::uint64_t key_id,
                                            CryptoLimits limits) {
#if !defined(MEAT2D_HAS_SODIUM)
    (void)key;
#endif
    if (!valid_limits(limits) || plaintext.size() > limits.maximum_plaintext_bytes ||
        associated_data.size() > limits.maximum_aad_bytes ||
        plaintext.size() > std::numeric_limits<unsigned long long>::max() ||
        associated_data.size() > std::numeric_limits<unsigned long long>::max() ||
        plaintext.size() > limits.maximum_ciphertext_bytes - aead_tag_bytes ||
        !fits_working_budget(plaintext.size(), plaintext.size(), aead_tag_bytes,
                             limits.maximum_working_bytes) ||
        !sodium_ready()) {
        return std::nullopt;
    }
    EncryptedBlock block{};
    try {
        block.key_id = key_id;
        block.plaintext_size = plaintext.size();
#if defined(MEAT2D_HAS_SODIUM)
        randombytes_buf(block.nonce.data(), block.nonce.size());
        block.ciphertext.resize(plaintext.size() + aead_tag_bytes);
        unsigned long long ciphertext_size{};
        if (crypto_aead_xchacha20poly1305_ietf_encrypt(
                block.ciphertext.data(), &ciphertext_size, plaintext.data(), plaintext.size(),
                associated_data.data(), associated_data.size(), nullptr, block.nonce.data(),
                key.data()) != 0 || ciphertext_size != block.ciphertext.size()) {
            secure_zero(block.ciphertext);
            return std::nullopt;
        }
#endif
        return block;
    } catch (...) {
        secure_zero(block.ciphertext);
        return std::nullopt;
    }
}

std::optional<std::vector<std::uint8_t>> decrypt_block(
    const EncryptedBlock& block,
    std::span<const std::uint8_t> associated_data,
    const Key& key,
    CryptoLimits limits) {
#if !defined(MEAT2D_HAS_SODIUM)
    (void)key;
#endif
    if (!valid_block(block, limits) || associated_data.size() > limits.maximum_aad_bytes ||
        associated_data.size() > std::numeric_limits<unsigned long long>::max() ||
        !sodium_ready()) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> plaintext;
    try {
        plaintext.resize(static_cast<std::size_t>(block.plaintext_size));
#if defined(MEAT2D_HAS_SODIUM)
        unsigned long long plaintext_size{};
        if (crypto_aead_xchacha20poly1305_ietf_decrypt(
                plaintext.data(), &plaintext_size, nullptr, block.ciphertext.data(),
                block.ciphertext.size(), associated_data.data(), associated_data.size(),
                block.nonce.data(), key.data()) != 0 || plaintext_size != plaintext.size()) {
            secure_zero(plaintext);
            return std::nullopt;
        }
#endif
        return plaintext;
    } catch (...) {
        secure_zero(plaintext);
        return std::nullopt;
    }
}

std::optional<std::vector<std::uint8_t>> encode_block(const EncryptedBlock& block,
                                                      CryptoLimits limits) {
    if (!valid_block(block, limits) || block.ciphertext.size() >
                                         std::numeric_limits<std::uint64_t>::max() -
                                             block_header_bytes) {
        return std::nullopt;
    }
    try {
        std::vector<std::uint8_t> encoded;
        encoded.reserve(block_header_bytes + block.ciphertext.size());
        encoded.insert(encoded.end(), block_magic.begin(), block_magic.end());
        append_u16(encoded, block.format_version);
        encoded.push_back(0U);
        encoded.push_back(0U);
        append_u64(encoded, block.key_id);
        append_u64(encoded, block.plaintext_size);
        append_u64(encoded, block.ciphertext.size());
        encoded.insert(encoded.end(), block.nonce.begin(), block.nonce.end());
        encoded.insert(encoded.end(), block.ciphertext.begin(), block.ciphertext.end());
        return encoded;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<EncryptedBlock> decode_block(std::span<const std::uint8_t> encoded,
                                           CryptoLimits limits) {
    if (!valid_limits(limits) || encoded.size() < block_header_bytes ||
        encoded.size() - block_header_bytes > limits.maximum_ciphertext_bytes) {
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
        EncryptedBlock block{};
        std::uint8_t flags{};
        std::uint8_t reserved{};
        std::uint64_t ciphertext_size{};
        if (!reader.read_u16(block.format_version) || !reader.read_u8(flags) ||
            !reader.read_u8(reserved) || !reader.read_u64(block.key_id) ||
            !reader.read_u64(block.plaintext_size) || !reader.read_u64(ciphertext_size) ||
            !reader.read_bytes(block.nonce) || flags != 0U || reserved != 0U ||
            block.format_version != crypto_format_version ||
            ciphertext_size > limits.maximum_ciphertext_bytes ||
            ciphertext_size != reader.remaining() ||
            ciphertext_size < aead_tag_bytes ||
            block.plaintext_size > limits.maximum_plaintext_bytes ||
            block.plaintext_size > std::numeric_limits<std::size_t>::max() ||
            ciphertext_size - aead_tag_bytes != block.plaintext_size ||
            !fits_working_budget(static_cast<std::size_t>(ciphertext_size),
                                 static_cast<std::size_t>(block.plaintext_size),
                                 limits.maximum_working_bytes)) {
            return std::nullopt;
        }
        block.ciphertext.assign(reader.rest().begin(), reader.rest().end());
        return valid_block(block, limits) ? std::optional(std::move(block)) : std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace meat2d::security
