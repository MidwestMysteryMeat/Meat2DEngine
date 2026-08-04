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

#if defined(MEAT2D_TEST_HAS_SODIUM)
    const std::string known_message =
        "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the "
        "future, sunscreen would be it.";
    const std::vector<std::uint8_t> known_plaintext(known_message.begin(), known_message.end());
    const meat2d::security::EncryptedBlock known_vector{
        .format_version = meat2d::security::crypto_format_version,
        .key_id = 7,
        .plaintext_size = known_plaintext.size(),
        .nonce = meat2d::security::Nonce{
            0x07, 0x00, 0x00, 0x00, 0x40, 0x41, 0x42, 0x43,
            0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b,
            0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53},
        .ciphertext = {
            0xf8, 0xeb, 0xea, 0x48, 0x75, 0x04, 0x40, 0x66, 0xfc, 0x16, 0x2a, 0x06,
            0x04, 0xe1, 0x71, 0xfe, 0xec, 0xfb, 0x3d, 0x20, 0x42, 0x52, 0x48, 0x56,
            0x3b, 0xcf, 0xd5, 0xa1, 0x55, 0xdc, 0xc4, 0x7b, 0xbd, 0xa7, 0x0b, 0x86,
            0xe5, 0xab, 0x9b, 0x55, 0x00, 0x2b, 0xd1, 0x27, 0x4c, 0x02, 0xdb, 0x35,
            0x32, 0x1a, 0xcd, 0x7a, 0xf8, 0xb2, 0xe2, 0xd2, 0x50, 0x15, 0xe1, 0x36,
            0xb7, 0x67, 0x94, 0x58, 0xe9, 0xf4, 0x32, 0x43, 0xbf, 0x71, 0x9d, 0x63,
            0x9b, 0xad, 0xb5, 0xfe, 0xac, 0x03, 0xf8, 0x0a, 0x19, 0xa9, 0x6e, 0xf1,
            0x0c, 0xb1, 0xd1, 0x53, 0x33, 0xa8, 0x37, 0xb9, 0x09, 0x46, 0xba, 0x38,
            0x54, 0xee, 0x74, 0xda, 0x3f, 0x25, 0x85, 0xef, 0xc7, 0xe1, 0xe1, 0x70,
            0xe1, 0x7e, 0x15, 0xe5, 0x63, 0xe7, 0x76, 0x01, 0xf4, 0xf8, 0x5c, 0xaf,
            0xa8, 0xe5, 0x87, 0x76, 0x14, 0xe1, 0x43, 0xe6, 0x84, 0x20},
    };
    const meat2d::security::Key known_key{
        0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
        0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
        0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
        0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f};
    const std::vector<std::uint8_t> known_ad{
        0x50, 0x51, 0x52, 0x53, 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7};
    const auto known_restored = meat2d::security::decrypt_block(
        known_vector, known_ad, known_key);
    check(known_restored && *known_restored == known_plaintext,
          "AEAD adapter failed libsodium's official XChaCha test vector");
#endif
}

} // namespace meat2d_tests
