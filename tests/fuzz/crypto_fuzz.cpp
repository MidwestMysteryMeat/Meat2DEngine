#include "meat2d/security/Crypto.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    constexpr std::size_t maximum_input = 1U * 1024U * 1024U;
    if (size > maximum_input) {
        return 0;
    }
    if (size == 0U) {
        return 0;
    }
    const meat2d::security::CryptoLimits limits{
        .maximum_plaintext_bytes = maximum_input,
        .maximum_ciphertext_bytes = maximum_input,
        .maximum_aad_bytes = 4096U,
        .maximum_working_bytes = 2U * maximum_input,
    };
    const auto encoded = std::span<const std::uint8_t>(data, size);
    const auto block = meat2d::security::decode_block(encoded, limits);
    if (block) {
        const meat2d::security::Key key{};
        const std::uint8_t aad_bytes[] = {'M', '2', 'P', 'K'};
        (void)meat2d::security::decrypt_block(block.value(), aad_bytes, key, limits);
    }
    return 0;
}
