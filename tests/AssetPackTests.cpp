#include "TestSupport.hpp"

#include "meat2d/tools/AssetPack.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace meat2d_tests {

void test_asset_packs() {
    const std::vector<std::uint8_t> first{'z', 'e', 't', 'a'};
    const std::vector<std::uint8_t> second{'a', 'l', 'p', 'h', 'a'};
    const std::vector<std::uint8_t> nested{'b', 'e', 't', 'a'};
    const std::vector<meat2d::tools::AssetPackInput> inputs{
        {.path = "zeta.txt", .bytes = first},
        {.path = "alpha.txt", .bytes = second},
        {.path = "nested\\beta.txt", .bytes = nested},
    };

    const auto encoded = meat2d::tools::build_asset_pack(inputs);
    check(encoded.has_value(), "asset pack rejected valid normalized inputs");
    if (!encoded) {
        return;
    }
    const auto encoded_again = meat2d::tools::build_asset_pack(inputs);
    check(encoded_again && *encoded_again == *encoded,
          "unencrypted asset pack output was not deterministic");

    const auto pack = meat2d::tools::open_asset_pack(*encoded);
    check(pack.has_value(), "asset pack could not be opened");
    if (!pack) {
        return;
    }
    check(pack->entries.size() == inputs.size() && pack->entries[0].path == "alpha.txt" &&
              pack->entries[1].path == "nested/beta.txt" && pack->entries[2].path == "zeta.txt",
          "asset pack entries were not normalized and sorted");

    const auto restored_first = meat2d::tools::read_asset_pack_entry(*pack, "alpha.txt");
    const auto restored_nested = meat2d::tools::read_asset_pack_entry(*pack, "nested/beta.txt");
    check(restored_first && *restored_first == second && restored_nested &&
              *restored_nested == nested,
          "asset pack entry did not round-trip through compression");
    const std::vector<meat2d::tools::AssetPackInput> traversal_inputs{
        {.path = "../escape", .bytes = first}};
    check(!meat2d::tools::build_asset_pack(traversal_inputs),
          "asset pack accepted a traversal path");
    const std::vector<meat2d::tools::AssetPackInput> duplicate_inputs{
        {.path = "same", .bytes = first}, {.path = "same", .bytes = second}};
    check(!meat2d::tools::build_asset_pack(duplicate_inputs),
          "asset pack accepted duplicate paths");

    meat2d::security::Key key{};
    for (std::size_t index = 0; index < key.size(); ++index) {
        key[index] = static_cast<std::uint8_t>(index + 3U);
    }
    const std::vector<meat2d::tools::AssetPackInput> encrypted_inputs{
        {.path = "private/data.bin", .bytes = second, .encrypt = true},
    };
    const meat2d::tools::AssetPackBuildOptions encrypted_options{
        .encryption_key = &key,
        .encryption_key_id = 9,
    };
    const auto encrypted = meat2d::tools::build_asset_pack(encrypted_inputs, encrypted_options);
#if defined(MEAT2D_TEST_HAS_SODIUM)
    check(encrypted.has_value(), "encrypted asset pack could not be built");
    if (!encrypted) {
        return;
    }
    const auto encrypted_pack = meat2d::tools::open_asset_pack(*encrypted);
    check(encrypted_pack && encrypted_pack->entries.size() == 1U &&
              encrypted_pack->entries.front().encrypted,
          "encrypted asset pack lost its encryption metadata");
    if (!encrypted_pack) {
        return;
    }
    const auto decrypted = meat2d::tools::read_asset_pack_entry(
        *encrypted_pack, "private/data.bin", &key);
    check(decrypted && *decrypted == second, "encrypted asset pack entry did not decrypt");
    check(!meat2d::tools::read_asset_pack_entry(*encrypted_pack, "private/data.bin"),
          "encrypted asset pack allowed a missing key");
    auto wrong_key = key;
    wrong_key.front() ^= 1U;
    check(!meat2d::tools::read_asset_pack_entry(
              *encrypted_pack, "private/data.bin", &wrong_key),
          "encrypted asset pack accepted the wrong key");
    auto tampered = *encrypted;
    tampered[encrypted_pack->entries.front().payload_offset] ^= 1U;
    const auto tampered_pack = meat2d::tools::open_asset_pack(tampered);
    check(tampered_pack && !meat2d::tools::read_asset_pack_entry(
                                *tampered_pack, "private/data.bin", &key),
          "tampered encrypted asset pack entry was accepted");
#else
    check(!encrypted.has_value(), "asset pack silently downgraded a requested encryption");
#endif
}

} // namespace meat2d_tests
