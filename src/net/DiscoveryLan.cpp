#include "DiscoveryInternal.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace meat2d::net {

using discovery_internal::control_datagram;
using discovery_internal::make_request_id;
using discovery_internal::same_server;

bool LanServerAdvertiser::start(std::uint16_t discovery_port) {
    stop();
    if (!socket_.open(discovery_port)) {
        last_error_ = std::string(socket_.last_error());
        return false;
    }
    last_error_.clear();
    return true;
}

void LanServerAdvertiser::stop() noexcept {
    socket_.close();
}

std::uint32_t LanServerAdvertiser::update(const ServerInfo& server) {
    if (!running() || !valid_server_info(server)) {
        return 0;
    }
    std::uint32_t replies = 0;
    for (std::size_t count = 0; count < 256U; ++count) {
        auto datagram = socket_.receive();
        if (!datagram) {
            break;
        }
        const auto packet = decode_packet(datagram->bytes);
        if (!packet || packet->header.type != PacketType::LanQuery) {
            continue;
        }
        const auto query = decode_lan_query(packet->payload);
        if (!query || (query->build_id != 0U && query->build_id != server.build_id)) {
            continue;
        }
        auto advertised = server;
        advertised.endpoint.address = "0.0.0.0";
        const auto payload = encode_lan_announcement({
            .request_id = query->request_id,
            .server = std::move(advertised),
        });
        const auto encoded =
            payload ? control_datagram(PacketType::LanAnnouncement, *payload) : std::nullopt;
        if (encoded && socket_.send(datagram->sender, *encoded)) {
            ++replies;
        }
    }
    if (!socket_.last_error().empty()) {
        last_error_ = std::string(socket_.last_error());
    }
    return replies;
}

bool LanServerAdvertiser::running() const noexcept {
    return socket_.valid();
}

std::uint16_t LanServerAdvertiser::port() const noexcept {
    return socket_.local_port();
}

std::string_view LanServerAdvertiser::last_error() const noexcept {
    return last_error_;
}

bool LanServerBrowser::refresh(std::uint16_t discovery_port, std::uint32_t build_id) {
    stop();
    if (discovery_port == 0U || !socket_.open() || !socket_.enable_broadcast()) {
        last_error_ = std::string(socket_.last_error());
        stop();
        return false;
    }
    request_id_ = make_request_id();
    build_id_ = build_id;
    servers_.clear();
    const auto payload = encode_lan_query({
        .request_id = request_id_,
        .build_id = build_id_,
    });
    const auto datagram = control_datagram(PacketType::LanQuery, payload);
    if (!datagram) {
        last_error_ = "LAN query could not be encoded";
        stop();
        return false;
    }
    const bool broadcast_sent = socket_.send(
        {
            .address = "255.255.255.255",
            .port = discovery_port,
        },
        *datagram);
    const bool loopback_sent = socket_.send(
        {
            .address = "127.0.0.1",
            .port = discovery_port,
        },
        *datagram);
    if (!broadcast_sent && !loopback_sent) {
        last_error_ = std::string(socket_.last_error());
        stop();
        return false;
    }
    last_error_.clear();
    return true;
}

std::uint32_t LanServerBrowser::update() {
    std::uint32_t added = 0;
    for (std::size_t count = 0; count < 256U; ++count) {
        auto datagram = socket_.receive();
        if (!datagram) {
            break;
        }
        const auto packet = decode_packet(datagram->bytes);
        if (!packet || packet->header.type != PacketType::LanAnnouncement) {
            continue;
        }
        auto announcement = decode_lan_announcement(packet->payload);
        if (!announcement || announcement->request_id != request_id_ ||
            (build_id_ != 0U && announcement->server.build_id != build_id_)) {
            continue;
        }
        announcement->server.endpoint.address = datagram->sender.address;
        const auto found =
            std::find_if(servers_.begin(), servers_.end(), [&](const ServerInfo& existing) {
                return same_server(existing, announcement->server);
            });
        if (found == servers_.end()) {
            servers_.push_back(std::move(announcement->server));
            ++added;
        } else {
            *found = std::move(announcement->server);
        }
    }
    return added;
}

void LanServerBrowser::stop() noexcept {
    socket_.close();
}

bool LanServerBrowser::searching() const noexcept {
    return socket_.valid();
}

std::span<const ServerInfo> LanServerBrowser::servers() const noexcept {
    return servers_;
}

std::string_view LanServerBrowser::last_error() const noexcept {
    return last_error_;
}

} // namespace meat2d::net
