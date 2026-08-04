#include "TestSupport.hpp"

#include "meat2d/security/Crypto.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace meat2d_tests {

void test_crypto_blocks() {
    const std::vector<std::uint8_t> plaintext{'s', 'e', 'c', 'r', 'e', 't'};
    const std::vector<std::uint8_t> associated_data{
        'M', '2', 'P', 'K', 1U, 0U, 'a', 's', 's', 'e', 't'};
    meat2d::security::Key key{};
    for (std::size_t index = 0; index < key.size(); ++index) {
        key[index] = static_cast<std::uint8_t>(index + 1U);
    }

    const auto generated = meat2d::security::generate_key();
#if defined(MEAT2D_TEST_HAS_SODIUM)
    check(generated.has_value(), "libsodium key generation failed");
#else
    check(!generated.has_value(), "crypto unexpectedly enabled without libsodium");
    return;
#endif

    const auto encrypted = meat2d::security::encrypt_block(
        plaintext, associated_data, key, 42U);
    check(encrypted.has_value(), "AEAD encryption rejected a bounded block");
    if (!encrypted) {
        return;
    }
    check(encrypted->ciphertext.size() == plaintext.size() + meat2d::security::aead_tag_bytes,
          "AEAD ciphertext did not include exactly one authentication tag");

    const auto encoded = meat2d::security::encode_block(*encrypted);
    check(encoded.has_value(), "AEAD envelope could not be encoded");
    if (!encoded) {
        return;
    }
    const auto decoded = meat2d::security::decode_block(*encoded);
    check(decoded.has_value(), "AEAD envelope could not be decoded");
    if (!decoded) {
        return;
    }
    const auto restored = meat2d::security::decrypt_block(*decoded, associated_data, key);
    check(restored && *restored == plaintext, "AEAD block did not round-trip plaintext");

    auto wrong_data = associated_data;
    wrong_data.back() ^= 1U;
    check(!meat2d::security::decrypt_block(*decoded, wrong_data, key),
          "AEAD accepted altered associated data");

    auto wrong_key = key;
    wrong_key.front() ^= 1U;
    check(!meat2d::security::decrypt_block(*decoded, associated_data, wrong_key),
          "AEAD accepted the wrong key");

    auto tampered = *encoded;
    tampered.back() ^= 1U;
    const auto tampered_block = meat2d::security::decode_block(tampered);
    check(tampered_block &&
              !meat2d::security::decrypt_block(*tampered_block, associated_data, key),
          "AEAD accepted tampered ciphertext");

    tampered.pop_back();
    check(!meat2d::security::decode_block(tampered), "truncated AEAD envelope was accepted");

    auto oversized = *encoded;
    for (std::size_t index = 16U; index < 24U; ++index) {
        oversized[index] = 0xFFU;
    }
    check(!meat2d::security::decode_block(oversized),
          "AEAD envelope accepted an oversized plaintext declaration");

    const meat2d::security::CryptoLimits tiny_limits{
        .maximum_plaintext_bytes = 4,
        .maximum_ciphertext_bytes = 64,
        .maximum_aad_bytes = 64,
        .maximum_working_bytes = 128,
    };
    check(!meat2d::security::encrypt_block(
              plaintext, associated_data, key, 42U, tiny_limits),
          "AEAD encryption ignored its plaintext budget");
}

} // namespace meat2d_tests
