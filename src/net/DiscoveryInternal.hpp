#pragma once

#include "meat2d/net/Discovery.hpp"

namespace meat2d::net::discovery_internal {

[[nodiscard]] std::uint64_t make_request_id() noexcept;
[[nodiscard]] std::optional<std::vector<std::uint8_t>>
control_datagram(PacketType type, std::span<const std::uint8_t> payload);
[[nodiscard]] bool same_server(const ServerInfo& left, const ServerInfo& right) noexcept;

} // namespace meat2d::net::discovery_internal
