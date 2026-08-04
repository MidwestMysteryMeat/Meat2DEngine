#include "DiscoveryInternal.hpp"

#include <atomic>
#include <chrono>

namespace meat2d::net::discovery_internal {

std::uint64_t make_request_id() noexcept {
    static std::atomic_uint64_t counter{1};
    auto value =
        static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    value ^= counter.fetch_add(1, std::memory_order_relaxed) * 0x9E3779B185EBCA87ULL;
    value ^= value >> 30U;
    value *= 0xBF58476D1CE4E5B9ULL;
    value ^= value >> 27U;
    value *= 0x94D049BB133111EBULL;
    value ^= value >> 31U;
    return value == 0U ? 1U : value;
}

std::optional<std::vector<std::uint8_t>>
control_datagram(PacketType type, std::span<const std::uint8_t> payload) {
    PacketHeader header{};
    header.type = type;
    return encode_packet(header, payload);
}

bool same_server(const ServerInfo& left, const ServerInfo& right) noexcept {
    return left.server_id == right.server_id;
}

} // namespace meat2d::net::discovery_internal
