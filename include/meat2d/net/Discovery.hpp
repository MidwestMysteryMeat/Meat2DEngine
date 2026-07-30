#pragma once

#include "meat2d/net/PacketCodec.hpp"
#include "meat2d/net/UdpSocket.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace meat2d::net {

inline constexpr std::size_t maximum_server_name_bytes = 48;
inline constexpr std::size_t maximum_mode_name_bytes = 24;
inline constexpr std::size_t maximum_map_name_bytes = 32;
inline constexpr std::size_t maximum_endpoint_address_bytes = 45;
inline constexpr std::size_t maximum_directory_page_entries = 6;
inline constexpr std::uint32_t directory_end_cursor = 0xFFFFFFFFU;

struct ServerInfo {
    std::uint64_t server_id{};
    Endpoint endpoint;
    std::string name{"Meat2D Server"};
    std::string mode{"Sandbox"};
    std::string map{"Elements Lab"};
    std::uint32_t build_id{1};
    std::uint8_t current_players{};
    std::uint8_t maximum_clients{maximum_players};
    bool password_protected{};
    bool nat_punch_available{};

    friend bool operator==(const ServerInfo&, const ServerInfo&) = default;
};

struct LanQueryMessage {
    std::uint64_t request_id{};
    std::uint32_t build_id{};
};

struct LanAnnouncementMessage {
    std::uint64_t request_id{};
    ServerInfo server;
};

struct DirectoryRegistrationMessage {
    std::uint64_t registration_secret{};
    ServerInfo server;
};

struct DirectoryListRequestMessage {
    std::uint64_t request_id{};
    std::uint32_t cursor{};
    std::uint32_t build_id{};
};

struct DirectoryListResponseMessage {
    std::uint64_t request_id{};
    std::uint32_t next_cursor{directory_end_cursor};
    std::vector<ServerInfo> servers;
};

struct DirectoryJoinRequestMessage {
    std::uint64_t request_id{};
    std::uint64_t server_id{};
};

struct DirectoryPunchMessage {
    std::uint64_t request_id{};
    std::uint64_t server_id{};
    Endpoint peer;
};

struct HolePunchMessage {
    std::uint64_t request_id{};
    std::uint64_t server_id{};
};

[[nodiscard]] bool valid_server_info(const ServerInfo& server) noexcept;
[[nodiscard]] std::vector<std::uint8_t> encode_lan_query(const LanQueryMessage& message);
[[nodiscard]] std::optional<LanQueryMessage>
decode_lan_query(std::span<const std::uint8_t> payload);
[[nodiscard]] std::optional<std::vector<std::uint8_t>>
encode_lan_announcement(const LanAnnouncementMessage& message);
[[nodiscard]] std::optional<LanAnnouncementMessage>
decode_lan_announcement(std::span<const std::uint8_t> payload);
[[nodiscard]] std::optional<std::vector<std::uint8_t>>
encode_directory_registration(const DirectoryRegistrationMessage& message);
[[nodiscard]] std::optional<DirectoryRegistrationMessage>
decode_directory_registration(std::span<const std::uint8_t> payload);
[[nodiscard]] std::vector<std::uint8_t>
encode_directory_list_request(const DirectoryListRequestMessage& message);
[[nodiscard]] std::optional<DirectoryListRequestMessage>
decode_directory_list_request(std::span<const std::uint8_t> payload);
[[nodiscard]] std::optional<std::vector<std::uint8_t>>
encode_directory_list_response(const DirectoryListResponseMessage& message);
[[nodiscard]] std::optional<DirectoryListResponseMessage>
decode_directory_list_response(std::span<const std::uint8_t> payload);
[[nodiscard]] std::vector<std::uint8_t>
encode_directory_join_request(const DirectoryJoinRequestMessage& message);
[[nodiscard]] std::optional<DirectoryJoinRequestMessage>
decode_directory_join_request(std::span<const std::uint8_t> payload);
[[nodiscard]] std::optional<std::vector<std::uint8_t>>
encode_directory_punch(const DirectoryPunchMessage& message);
[[nodiscard]] std::optional<DirectoryPunchMessage>
decode_directory_punch(std::span<const std::uint8_t> payload);
[[nodiscard]] std::vector<std::uint8_t> encode_hole_punch(const HolePunchMessage& message);
[[nodiscard]] std::optional<HolePunchMessage>
decode_hole_punch(std::span<const std::uint8_t> payload);

class LanServerAdvertiser {
  public:
    bool start(std::uint16_t discovery_port = default_lan_discovery_port);
    void stop() noexcept;
    std::uint32_t update(const ServerInfo& server);

    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] std::uint16_t port() const noexcept;
    [[nodiscard]] std::string_view last_error() const noexcept;

  private:
    UdpSocket socket_;
    std::string last_error_;
};

class LanServerBrowser {
  public:
    bool refresh(std::uint16_t discovery_port = default_lan_discovery_port,
                 std::uint32_t build_id = 1);
    std::uint32_t update();
    void stop() noexcept;

    [[nodiscard]] bool searching() const noexcept;
    [[nodiscard]] std::span<const ServerInfo> servers() const noexcept;
    [[nodiscard]] std::string_view last_error() const noexcept;

  private:
    UdpSocket socket_;
    std::uint64_t request_id_{};
    std::uint32_t build_id_{};
    std::vector<ServerInfo> servers_;
    std::string last_error_;
};

class PublicServerBrowser {
  public:
    bool refresh(Endpoint directory, std::uint32_t build_id = 1);
    std::uint32_t update();
    void stop() noexcept;

    [[nodiscard]] bool searching() const noexcept;
    [[nodiscard]] bool complete() const noexcept;
    [[nodiscard]] std::span<const ServerInfo> servers() const noexcept;
    [[nodiscard]] std::string_view last_error() const noexcept;

  private:
    bool request_page(std::uint32_t cursor);
    bool send_current_page();

    UdpSocket socket_;
    Endpoint directory_;
    std::uint64_t request_id_{};
    std::uint32_t build_id_{};
    std::uint32_t current_cursor_{};
    std::uint32_t pages_received_{};
    std::uint8_t request_attempts_{};
    std::chrono::steady_clock::time_point last_request_;
    bool complete_{};
    std::vector<ServerInfo> servers_;
    std::string last_error_;
};

struct DirectoryConfig {
    std::uint16_t port{default_directory_port};
    std::size_t maximum_servers{1'024};
    std::size_t maximum_datagrams_per_update{256};
    std::chrono::milliseconds lease_timeout{15'000};
};

struct DirectoryUpdateStats {
    std::uint32_t datagrams_received{};
    std::uint32_t datagrams_sent{};
    std::uint32_t invalid_datagrams{};
    std::uint32_t registrations{};
    std::uint32_t list_requests{};
    std::uint32_t join_requests{};
    std::uint32_t expired_servers{};
};

class PublicDirectoryServer {
  public:
    explicit PublicDirectoryServer(DirectoryConfig config = {});
    ~PublicDirectoryServer();
    PublicDirectoryServer(const PublicDirectoryServer&) = delete;
    PublicDirectoryServer& operator=(const PublicDirectoryServer&) = delete;

    bool start();
    void stop() noexcept;
    DirectoryUpdateStats update();

    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] std::uint16_t port() const noexcept;
    [[nodiscard]] std::size_t server_count() const noexcept;
    [[nodiscard]] std::string_view last_error() const noexcept;

  private:
    struct RegisteredServer {
        ServerInfo server;
        std::uint64_t registration_secret{};
        std::chrono::steady_clock::time_point last_seen;
    };

    void expire(DirectoryUpdateStats& stats);
    void handle_registration(const Endpoint& sender, std::span<const std::uint8_t> payload,
                             DirectoryUpdateStats& stats);
    void handle_list_request(const Endpoint& sender, std::span<const std::uint8_t> payload,
                             DirectoryUpdateStats& stats);
    void handle_join_request(const Endpoint& sender, std::span<const std::uint8_t> payload,
                             DirectoryUpdateStats& stats);
    bool send_message(const Endpoint& endpoint, PacketType type,
                      std::span<const std::uint8_t> payload, DirectoryUpdateStats& stats);

    DirectoryConfig config_;
    UdpSocket socket_;
    std::vector<RegisteredServer> servers_;
    std::string last_error_;
};

} // namespace meat2d::net
