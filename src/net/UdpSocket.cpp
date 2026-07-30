#include "meat2d/net/UdpSocket.hpp"

#include <array>
#include <cstring>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <WS2tcpip.h>
#include <WinSock2.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace meat2d::net {
namespace {

#if defined(_WIN32)
using NativeSocket = SOCKET;
using SocketLength = int;
constexpr NativeSocket invalid_socket = INVALID_SOCKET;

struct WindowsSockets {
    WindowsSockets() {
        WSADATA data{};
        initialized = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    ~WindowsSockets() {
        if (initialized) {
            WSACleanup();
        }
    }
    bool initialized{};
};

bool sockets_ready() {
    static WindowsSockets sockets;
    return sockets.initialized;
}

int socket_error() noexcept {
    return WSAGetLastError();
}

bool would_block(int error) noexcept {
    return error == WSAEWOULDBLOCK;
}

void close_socket(NativeSocket socket) noexcept {
    closesocket(socket);
}
#else
using NativeSocket = int;
using SocketLength = socklen_t;
constexpr NativeSocket invalid_socket = -1;

bool sockets_ready() {
    return true;
}

int socket_error() noexcept {
    return errno;
}

bool would_block(int error) noexcept {
    return error == EAGAIN || error == EWOULDBLOCK;
}

void close_socket(NativeSocket socket) noexcept {
    ::close(socket);
}
#endif

std::string describe_error(const char* operation, int error) {
    return std::string(operation) + " failed with socket error " + std::to_string(error);
}

} // namespace

struct UdpSocket::Impl {
    NativeSocket socket{invalid_socket};
    std::uint16_t local_port{};
    std::string last_error;
};

std::optional<Endpoint> resolve_endpoint(std::string_view host, std::uint16_t port) {
    if (host.empty() || port == 0U || !sockets_ready()) {
        return std::nullopt;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    const std::string host_text(host);
    if (inet_pton(AF_INET, host_text.c_str(), &address.sin_addr) != 1) {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        addrinfo* result = nullptr;
        const auto service = std::to_string(port);
        const auto status = getaddrinfo(host_text.c_str(), service.c_str(), &hints, &result);
        if (status != 0 || result == nullptr) {
            if (result != nullptr) {
                freeaddrinfo(result);
            }
            return std::nullopt;
        }
        std::memcpy(&address, result->ai_addr, sizeof(address));
        freeaddrinfo(result);
    }

    std::array<char, INET_ADDRSTRLEN> address_text{};
    if (inet_ntop(AF_INET, &address.sin_addr, address_text.data(),
                  static_cast<SocketLength>(address_text.size())) == nullptr) {
        return std::nullopt;
    }
    return Endpoint{
        .address = address_text.data(),
        .port = port,
    };
}

UdpSocket::UdpSocket() : impl_(std::make_unique<Impl>()) {}

UdpSocket::~UdpSocket() {
    close();
}

UdpSocket::UdpSocket(UdpSocket&& other) noexcept : impl_(std::move(other.impl_)) {}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept {
    if (this != &other) {
        close();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

bool UdpSocket::open(std::uint16_t port, std::string_view bind_address) {
    if (impl_ == nullptr) {
        impl_ = std::make_unique<Impl>();
    }
    close();
    if (!sockets_ready()) {
        impl_->last_error = "socket runtime initialization failed";
        return false;
    }

    impl_->socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (impl_->socket == invalid_socket) {
        impl_->last_error = describe_error("socket", socket_error());
        return false;
    }

#if defined(_WIN32)
    u_long nonblocking = 1;
    if (ioctlsocket(impl_->socket, FIONBIO, &nonblocking) != 0) {
        impl_->last_error = describe_error("ioctlsocket", socket_error());
        close();
        return false;
    }
#else
    const auto flags = fcntl(impl_->socket, F_GETFL, 0);
    if (flags < 0 || fcntl(impl_->socket, F_SETFL, flags | O_NONBLOCK) < 0) {
        impl_->last_error = describe_error("fcntl", socket_error());
        close();
        return false;
    }
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    const std::string bind_text(bind_address);
    if (bind_text.empty() || bind_text == "0.0.0.0") {
        address.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (inet_pton(AF_INET, bind_text.c_str(), &address.sin_addr) != 1) {
        impl_->last_error = "bind address is not a valid IPv4 address";
        close();
        return false;
    }

    if (::bind(impl_->socket, reinterpret_cast<const sockaddr*>(&address),
               static_cast<SocketLength>(sizeof(address))) != 0) {
        impl_->last_error = describe_error("bind", socket_error());
        close();
        return false;
    }

    sockaddr_in bound{};
    SocketLength bound_size = static_cast<SocketLength>(sizeof(bound));
    if (getsockname(impl_->socket, reinterpret_cast<sockaddr*>(&bound), &bound_size) != 0) {
        impl_->last_error = describe_error("getsockname", socket_error());
        close();
        return false;
    }
    impl_->local_port = ntohs(bound.sin_port);
    impl_->last_error.clear();
    return true;
}

void UdpSocket::close() noexcept {
    if (impl_ != nullptr && impl_->socket != invalid_socket) {
        close_socket(impl_->socket);
        impl_->socket = invalid_socket;
        impl_->local_port = 0;
    }
}

bool UdpSocket::valid() const noexcept {
    return impl_ != nullptr && impl_->socket != invalid_socket;
}

std::uint16_t UdpSocket::local_port() const noexcept {
    return impl_ != nullptr ? impl_->local_port : 0U;
}

bool UdpSocket::enable_broadcast(bool enabled) {
    if (!valid()) {
        if (impl_ != nullptr) {
            impl_->last_error = "cannot configure a closed UDP socket";
        }
        return false;
    }
    const int value = enabled ? 1 : 0;
#if defined(_WIN32)
    const auto result =
        setsockopt(impl_->socket, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&value),
                   static_cast<int>(sizeof(value)));
#else
    const auto result = setsockopt(impl_->socket, SOL_SOCKET, SO_BROADCAST, &value,
                                   static_cast<SocketLength>(sizeof(value)));
#endif
    if (result != 0) {
        impl_->last_error = describe_error("setsockopt(SO_BROADCAST)", socket_error());
        return false;
    }
    impl_->last_error.clear();
    return true;
}

bool UdpSocket::send(const Endpoint& endpoint, std::span<const std::uint8_t> bytes) {
    if (!valid() || endpoint.port == 0U || bytes.empty() || bytes.size() > maximum_datagram_bytes) {
        if (impl_ != nullptr) {
            impl_->last_error = "invalid UDP send request";
        }
        return false;
    }

    const auto resolved = resolve_endpoint(endpoint.address, endpoint.port);
    if (!resolved) {
        impl_->last_error = "could not resolve UDP endpoint";
        return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(resolved->port);
    inet_pton(AF_INET, resolved->address.c_str(), &address.sin_addr);

#if defined(_WIN32)
    const auto sent = sendto(
        impl_->socket, reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()),
        0, reinterpret_cast<const sockaddr*>(&address), static_cast<int>(sizeof(address)));
    const bool complete = sent == static_cast<int>(bytes.size());
#else
    const auto sent = sendto(impl_->socket, bytes.data(), bytes.size(), 0,
                             reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    const bool complete = sent == static_cast<ssize_t>(bytes.size());
#endif
    if (!complete) {
        impl_->last_error = describe_error("sendto", socket_error());
        return false;
    }
    impl_->last_error.clear();
    return true;
}

std::optional<Datagram> UdpSocket::receive() {
    if (!valid()) {
        return std::nullopt;
    }

    std::array<std::uint8_t, maximum_datagram_bytes + 1U> buffer{};
    sockaddr_in sender{};
    SocketLength sender_size = static_cast<SocketLength>(sizeof(sender));
#if defined(_WIN32)
    const auto received = recvfrom(impl_->socket, reinterpret_cast<char*>(buffer.data()),
                                   static_cast<int>(buffer.size()), 0,
                                   reinterpret_cast<sockaddr*>(&sender), &sender_size);
    if (received == SOCKET_ERROR) {
#else
    const auto received = recvfrom(impl_->socket, buffer.data(), buffer.size(), 0,
                                   reinterpret_cast<sockaddr*>(&sender), &sender_size);
    if (received < 0) {
#endif
        const auto error = socket_error();
        if (!would_block(error)) {
            impl_->last_error = describe_error("recvfrom", error);
        } else {
            impl_->last_error.clear();
        }
        return std::nullopt;
    }

    std::array<char, INET_ADDRSTRLEN> address_text{};
    if (inet_ntop(AF_INET, &sender.sin_addr, address_text.data(),
                  static_cast<SocketLength>(address_text.size())) == nullptr) {
        impl_->last_error = describe_error("inet_ntop", socket_error());
        return std::nullopt;
    }
    impl_->last_error.clear();
    return Datagram{
        .sender =
            {
                .address = address_text.data(),
                .port = ntohs(sender.sin_port),
            },
        .bytes = std::vector<std::uint8_t>(buffer.begin(),
                                           buffer.begin() + static_cast<std::ptrdiff_t>(received)),
    };
}

std::string_view UdpSocket::last_error() const noexcept {
    return impl_ != nullptr ? std::string_view(impl_->last_error) : std::string_view{};
}

} // namespace meat2d::net
