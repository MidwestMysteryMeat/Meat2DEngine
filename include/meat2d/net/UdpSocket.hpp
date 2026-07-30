#pragma once

#include "meat2d/net/Protocol.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace meat2d::net {

struct Endpoint {
    std::string address{"127.0.0.1"};
    std::uint16_t port{};

    friend bool operator==(const Endpoint&, const Endpoint&) = default;
};

struct Datagram {
    Endpoint sender;
    std::vector<std::uint8_t> bytes;
};

[[nodiscard]] std::optional<Endpoint> resolve_endpoint(std::string_view host, std::uint16_t port);

class UdpSocket {
  public:
    UdpSocket();
    ~UdpSocket();
    UdpSocket(UdpSocket&&) noexcept;
    UdpSocket& operator=(UdpSocket&&) noexcept;
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    bool open(std::uint16_t port = 0, std::string_view bind_address = "0.0.0.0");
    void close() noexcept;
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] std::uint16_t local_port() const noexcept;
    bool enable_broadcast(bool enabled = true);

    bool send(const Endpoint& endpoint, std::span<const std::uint8_t> bytes);
    [[nodiscard]] std::optional<Datagram> receive();
    [[nodiscard]] std::string_view last_error() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace meat2d::net
