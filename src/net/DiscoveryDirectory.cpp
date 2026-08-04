#include "DiscoveryInternal.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>

namespace meat2d::net {

using discovery_internal::control_datagram;
using discovery_internal::make_request_id;
using discovery_internal::same_server;

bool PublicServerBrowser::refresh(Endpoint directory, std::uint32_t build_id) {
    stop();
    const auto resolved = resolve_endpoint(directory.address, directory.port);
    if (!resolved) {
        last_error_ = "could not resolve public directory endpoint";
        return false;
    }
    if (!socket_.open()) {
        last_error_ = std::string(socket_.last_error());
        return false;
    }
    directory_ = *resolved;
    build_id_ = build_id;
    current_cursor_ = 0;
    pages_received_ = 0;
    complete_ = false;
    servers_.clear();
    if (!request_page(0)) {
        stop();
        return false;
    }
    last_error_.clear();
    return true;
}

std::uint32_t PublicServerBrowser::update() {
    std::uint32_t added = 0;
    for (std::size_t count = 0; count < 256U; ++count) {
        auto datagram = socket_.receive();
        if (!datagram) {
            break;
        }
        if (datagram->sender != directory_) {
            continue;
        }
        const auto packet = decode_packet(datagram->bytes);
        if (!packet || packet->header.type != PacketType::DirectoryListResponse) {
            continue;
        }
        const auto response = decode_directory_list_response(packet->payload);
        if (!response || response->request_id != request_id_) {
            continue;
        }
        if (pages_received_ >= 128U) {
            complete_ = true;
            last_error_ = "directory pagination exceeded its safety limit";
            socket_.close();
            break;
        }
        ++pages_received_;
        for (const auto& server : response->servers) {
            if (build_id_ != 0U && server.build_id != build_id_) {
                continue;
            }
            const auto found =
                std::find_if(servers_.begin(), servers_.end(), [&](const ServerInfo& existing) {
                    return same_server(existing, server);
                });
            if (found == servers_.end()) {
                servers_.push_back(server);
                ++added;
            } else {
                *found = server;
            }
        }
        if (response->next_cursor == directory_end_cursor) {
            complete_ = true;
            socket_.close();
        } else if (response->next_cursor <= current_cursor_) {
            complete_ = true;
            last_error_ = "directory returned a non-advancing page cursor";
            socket_.close();
        } else if (!request_page(response->next_cursor)) {
            complete_ = true;
            socket_.close();
        }
    }
    if (searching() &&
        std::chrono::steady_clock::now() - last_request_ >= std::chrono::milliseconds(400)) {
        if (request_attempts_ >= 5U) {
            complete_ = true;
            last_error_ = "public directory did not answer";
            socket_.close();
        } else if (!send_current_page()) {
            complete_ = true;
            socket_.close();
        }
    }
    return added;
}

void PublicServerBrowser::stop() noexcept {
    socket_.close();
}

bool PublicServerBrowser::searching() const noexcept {
    return socket_.valid() && !complete_;
}

bool PublicServerBrowser::complete() const noexcept {
    return complete_;
}

std::span<const ServerInfo> PublicServerBrowser::servers() const noexcept {
    return servers_;
}

std::string_view PublicServerBrowser::last_error() const noexcept {
    return last_error_;
}

bool PublicServerBrowser::request_page(std::uint32_t cursor) {
    if (cursor == directory_end_cursor) {
        last_error_ = "directory page cursor is invalid";
        return false;
    }
    request_id_ = make_request_id();
    current_cursor_ = cursor;
    request_attempts_ = 0;
    return send_current_page();
}

bool PublicServerBrowser::send_current_page() {
    const auto payload = encode_directory_list_request({
        .request_id = request_id_,
        .cursor = current_cursor_,
        .build_id = build_id_,
    });
    const auto datagram = control_datagram(PacketType::DirectoryListRequest, payload);
    if (!datagram || !socket_.send(directory_, *datagram)) {
        last_error_ = datagram ? std::string(socket_.last_error())
                               : "directory list request could not be encoded";
        return false;
    }
    ++request_attempts_;
    last_request_ = std::chrono::steady_clock::now();
    return true;
}

PublicDirectoryServer::PublicDirectoryServer(DirectoryConfig config) : config_(config) {
    config_.maximum_servers = std::clamp<std::size_t>(config_.maximum_servers, 1U, 65'536U);
    config_.maximum_datagrams_per_update =
        std::clamp<std::size_t>(config_.maximum_datagrams_per_update, 1U, 4'096U);
    config_.lease_timeout = std::max(config_.lease_timeout, std::chrono::milliseconds(100));
    servers_.reserve(std::min<std::size_t>(config_.maximum_servers, 4'096U));
}

PublicDirectoryServer::~PublicDirectoryServer() = default;

bool PublicDirectoryServer::start() {
    stop();
    if (!socket_.open(config_.port)) {
        last_error_ = std::string(socket_.last_error());
        return false;
    }
    last_error_.clear();
    return true;
}

void PublicDirectoryServer::stop() noexcept {
    socket_.close();
    servers_.clear();
}

DirectoryUpdateStats PublicDirectoryServer::update() {
    DirectoryUpdateStats stats{};
    if (!running()) {
        return stats;
    }
    expire(stats);
    for (std::size_t count = 0; count < config_.maximum_datagrams_per_update; ++count) {
        auto datagram = socket_.receive();
        if (!datagram) {
            break;
        }
        ++stats.datagrams_received;
        const auto packet = decode_packet(datagram->bytes);
        if (!packet || packet->header.flags != PacketFlagNone) {
            ++stats.invalid_datagrams;
            continue;
        }
        switch (packet->header.type) {
        case PacketType::DirectoryRegister:
            handle_registration(datagram->sender, packet->payload, stats);
            break;
        case PacketType::DirectoryListRequest:
            handle_list_request(datagram->sender, packet->payload, stats);
            break;
        case PacketType::DirectoryJoinRequest:
            handle_join_request(datagram->sender, packet->payload, stats);
            break;
        default:
            ++stats.invalid_datagrams;
            break;
        }
    }
    return stats;
}

bool PublicDirectoryServer::running() const noexcept {
    return socket_.valid();
}

std::uint16_t PublicDirectoryServer::port() const noexcept {
    return socket_.local_port();
}

std::size_t PublicDirectoryServer::server_count() const noexcept {
    return servers_.size();
}

std::string_view PublicDirectoryServer::last_error() const noexcept {
    return last_error_;
}

void PublicDirectoryServer::expire(DirectoryUpdateStats& stats) {
    const auto now = std::chrono::steady_clock::now();
    const auto previous = servers_.size();
    servers_.erase(std::remove_if(servers_.begin(), servers_.end(),
                                  [&](const RegisteredServer& server) {
                                      return now - server.last_seen > config_.lease_timeout;
                                  }),
                   servers_.end());
    stats.expired_servers = static_cast<std::uint32_t>(std::min<std::size_t>(
        previous - servers_.size(), std::numeric_limits<std::uint32_t>::max()));
}

void PublicDirectoryServer::handle_registration(const Endpoint& sender,
                                                std::span<const std::uint8_t> payload,
                                                DirectoryUpdateStats& stats) {
    auto registration = decode_directory_registration(payload);
    if (!registration) {
        ++stats.invalid_datagrams;
        return;
    }
    auto found =
        std::find_if(servers_.begin(), servers_.end(), [&](const RegisteredServer& existing) {
            return existing.server.server_id == registration->server.server_id;
        });
    if (found != servers_.end() &&
        found->registration_secret != registration->registration_secret) {
        ++stats.invalid_datagrams;
        return;
    }
    registration->server.endpoint = sender;
    registration->server.nat_punch_available = true;
    if (found == servers_.end()) {
        if (servers_.size() >= config_.maximum_servers) {
            ++stats.invalid_datagrams;
            return;
        }
        servers_.push_back({
            .server = std::move(registration->server),
            .registration_secret = registration->registration_secret,
            .last_seen = std::chrono::steady_clock::now(),
        });
    } else {
        found->server = std::move(registration->server);
        found->last_seen = std::chrono::steady_clock::now();
    }
    ++stats.registrations;
}

void PublicDirectoryServer::handle_list_request(const Endpoint& sender,
                                                std::span<const std::uint8_t> payload,
                                                DirectoryUpdateStats& stats) {
    const auto request = decode_directory_list_request(payload);
    if (!request) {
        ++stats.invalid_datagrams;
        return;
    }
    std::vector<const ServerInfo*> matches;
    matches.reserve(servers_.size());
    for (const auto& registered : servers_) {
        if (request->build_id == 0U || registered.server.build_id == request->build_id) {
            matches.push_back(&registered.server);
        }
    }
    std::sort(matches.begin(), matches.end(), [](const ServerInfo* left, const ServerInfo* right) {
        return left->server_id < right->server_id;
    });

    const auto start = std::min<std::size_t>(request->cursor, matches.size());
    const auto end = std::min(start + maximum_directory_page_entries, matches.size());
    DirectoryListResponseMessage response{
        .request_id = request->request_id,
        .next_cursor =
            end < matches.size() ? static_cast<std::uint32_t>(end) : directory_end_cursor,
        .servers = {},
    };
    response.servers.reserve(end - start);
    for (auto index = start; index < end; ++index) {
        response.servers.push_back(*matches[index]);
    }
    const auto response_payload = encode_directory_list_response(response);
    if (!response_payload ||
        !send_message(sender, PacketType::DirectoryListResponse, *response_payload, stats)) {
        ++stats.invalid_datagrams;
        return;
    }
    ++stats.list_requests;
}

void PublicDirectoryServer::handle_join_request(const Endpoint& sender,
                                                std::span<const std::uint8_t> payload,
                                                DirectoryUpdateStats& stats) {
    const auto request = decode_directory_join_request(payload);
    if (!request) {
        ++stats.invalid_datagrams;
        return;
    }
    const auto found =
        std::find_if(servers_.begin(), servers_.end(), [&](const RegisteredServer& server) {
            return server.server.server_id == request->server_id;
        });
    if (found == servers_.end()) {
        ++stats.invalid_datagrams;
        return;
    }
    const auto to_server = encode_directory_punch({
        .request_id = request->request_id,
        .server_id = request->server_id,
        .peer = sender,
    });
    const auto to_client = encode_directory_punch({
        .request_id = request->request_id,
        .server_id = request->server_id,
        .peer = found->server.endpoint,
    });
    if (!to_server || !to_client ||
        !send_message(found->server.endpoint, PacketType::DirectoryPunch, *to_server, stats) ||
        !send_message(sender, PacketType::DirectoryPunch, *to_client, stats)) {
        ++stats.invalid_datagrams;
        return;
    }
    ++stats.join_requests;
}

bool PublicDirectoryServer::send_message(const Endpoint& endpoint, PacketType type,
                                         std::span<const std::uint8_t> payload,
                                         DirectoryUpdateStats& stats) {
    const auto datagram = control_datagram(type, payload);
    if (!datagram || !socket_.send(endpoint, *datagram)) {
        last_error_ =
            datagram ? std::string(socket_.last_error()) : "directory message could not be encoded";
        return false;
    }
    ++stats.datagrams_sent;
    return true;
}

} // namespace meat2d::net
