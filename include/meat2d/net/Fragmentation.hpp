#pragma once

#include "meat2d/net/Protocol.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace meat2d::net {

inline constexpr std::size_t fragment_header_bytes = 8;
inline constexpr std::size_t maximum_fragment_data_bytes =
    maximum_datagram_bytes - encoded_header_bytes - fragment_header_bytes;
inline constexpr std::uint16_t maximum_fragments_per_message = 64;
inline constexpr std::size_t maximum_fragmented_message_bytes =
    maximum_fragment_data_bytes * maximum_fragments_per_message;

struct MessageFragment {
    std::uint32_t message_id{};
    std::uint16_t fragment_index{};
    std::uint16_t fragment_count{};
    std::vector<std::uint8_t> data;
};

[[nodiscard]] std::vector<std::vector<std::uint8_t>> fragment_payload(
    std::uint32_t message_id,
    std::span<const std::uint8_t> payload);
[[nodiscard]] std::optional<MessageFragment> decode_fragment(
    std::span<const std::uint8_t> payload);

class FragmentAssembler {
  public:
    explicit FragmentAssembler(
        std::uint32_t expire_after_updates = 600,
        std::size_t maximum_assemblies = 16);

    std::optional<std::vector<std::uint8_t>> accept(
        std::span<const std::uint8_t> fragment_payload,
        std::uint32_t update);
    void expire(std::uint32_t update);
    [[nodiscard]] std::size_t pending_messages() const noexcept;

  private:
    struct Assembly {
        std::uint32_t message_id{};
        std::uint16_t fragment_count{};
        std::uint16_t received_count{};
        std::uint32_t last_update{};
        std::size_t total_bytes{};
        std::vector<std::vector<std::uint8_t>> fragments;
        std::vector<bool> received;
    };

    std::uint32_t expire_after_updates_{};
    std::size_t maximum_assemblies_{};
    std::vector<Assembly> assemblies_;
};

} // namespace meat2d::net
