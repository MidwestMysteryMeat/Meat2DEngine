#pragma once

#include "meat2d/ai/LivingSimulation.hpp"
#include "meat2d/net/ChunkCodec.hpp"
#include "meat2d/net/Discovery.hpp"
#include "meat2d/net/Fragmentation.hpp"
#include "meat2d/net/PacketCodec.hpp"
#include "meat2d/net/Reliability.hpp"
#include "meat2d/net/UdpSocket.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace meat2d::net {

inline constexpr ai::EntityId network_issuer_base = 0x80000000U;

struct ServerConfig {
    WorldConfig world{};
    std::uint16_t port{default_port};
    std::uint16_t tick_rate{60};
    std::uint8_t maximum_clients{maximum_players};
    std::uint8_t interest_radius_chunks{2};
    std::uint8_t maximum_brush_radius{8};
    std::uint8_t maximum_inputs_per_update{4};
    std::uint32_t snapshot_interval_ticks{3};
    std::uint32_t chunk_interval_ticks{1};
    std::uint32_t client_timeout_updates{600};
    std::string session_name{"Meat2D Server"};
    std::string mode_name{"Sandbox"};
    std::string map_name{"Elements Lab"};
    std::uint32_t build_id{1};
    bool password_protected{};
    bool advertise_lan{};
    std::uint16_t lan_discovery_port{default_lan_discovery_port};
    bool advertise_public{};
    std::optional<Endpoint> public_directory;
    std::uint32_t directory_heartbeat_updates{120};
};

struct ServerUpdateStats {
    ai::LivingStats simulation{};
    std::uint32_t datagrams_received{};
    std::uint32_t datagrams_sent{};
    std::uint32_t invalid_datagrams{};
    std::uint32_t accepted_inputs{};
    std::uint32_t rejected_inputs{};
    std::uint32_t chunk_messages{};
    std::uint32_t connected_clients{};
    std::uint32_t lan_replies{};
    std::uint32_t directory_heartbeats{};
    std::uint32_t nat_punches{};
};

class AuthoritativeServer {
  public:
    explicit AuthoritativeServer(ServerConfig config = {});
    ~AuthoritativeServer();
    AuthoritativeServer(const AuthoritativeServer&) = delete;
    AuthoritativeServer& operator=(const AuthoritativeServer&) = delete;

    bool start();
    void stop() noexcept;
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] std::uint16_t port() const noexcept;
    [[nodiscard]] std::size_t client_count() const noexcept;
    [[nodiscard]] std::uint64_t server_id() const noexcept;
    [[nodiscard]] ServerInfo server_info() const;
    [[nodiscard]] std::string_view last_error() const noexcept;

    [[nodiscard]] ai::LivingSimulation& simulation() noexcept;
    [[nodiscard]] const ai::LivingSimulation& simulation() const noexcept;
    ServerUpdateStats update();

  private:
    struct ClientSlot {
        std::uint8_t id{};
        Endpoint endpoint;
        std::uint64_t nonce{};
        std::uint64_t session_token{};
        std::string name;
        ReliableChannel channel{ReliabilityConfig{}};
        Vec2i focus{};
        std::vector<std::uint64_t> known_chunk_revisions;
        std::uint32_t last_heard_update{};
        std::uint32_t last_input_sequence{};
        std::uint32_t next_message_id{1};
        std::uint8_t inputs_this_update{};
        bool needs_ack{};
    };

    struct QueuedInput {
        std::uint8_t client_id{};
        InputMessage message;
    };

    [[nodiscard]] ClientSlot* find_client(const Endpoint& endpoint) noexcept;
    [[nodiscard]] ClientSlot* find_client(std::uint8_t id) noexcept;
    [[nodiscard]] std::uint8_t allocate_client_id() const noexcept;
    void poll_datagrams(ServerUpdateStats& stats);
    void handle_unknown(const Endpoint& endpoint, const Packet& packet, ServerUpdateStats& stats);
    void handle_client_packet(ClientSlot& client, const Packet& packet, ServerUpdateStats& stats);
    void apply_inputs(ServerUpdateStats& stats);
    void send_world_updates(ServerUpdateStats& stats);
    bool send_packet(ClientSlot& client, const Packet& packet, ServerUpdateStats& stats);
    void send_message(ClientSlot& client, PacketType type, std::span<const std::uint8_t> payload,
                      bool reliable, std::uint8_t additional_flags, ServerUpdateStats& stats);
    void send_chunk(ClientSlot& client, std::size_t chunk_index, ServerUpdateStats& stats);
    void flush_channels(ServerUpdateStats& stats);
    void remove_timed_out_clients();
    void send_directory_registration(ServerUpdateStats& stats);

    ServerConfig config_;
    ai::LivingSimulation simulation_;
    UdpSocket socket_;
    LanServerAdvertiser lan_advertiser_;
    std::optional<Endpoint> public_directory_;
    std::vector<ClientSlot> clients_;
    std::vector<QueuedInput> inputs_;
    std::uint32_t network_update_{};
    std::uint64_t server_secret_{};
    std::uint64_t server_id_{};
    std::uint64_t registration_secret_{};
    std::string last_error_;
};

enum class ClientConnectionState : std::uint8_t {
    Disconnected,
    Connecting,
    Connected,
    Rejected,
    TimedOut
};

struct ClientUpdateStats {
    std::uint32_t datagrams_received{};
    std::uint32_t datagrams_sent{};
    std::uint32_t invalid_datagrams{};
    std::uint32_t completed_chunks{};
    std::uint32_t changed_cells{};
    std::uint32_t chunk_hash_mismatches{};
    std::uint32_t reapplied_predictions{};
};

class AuthoritativeClient {
  public:
    AuthoritativeClient();
    ~AuthoritativeClient();
    AuthoritativeClient(const AuthoritativeClient&) = delete;
    AuthoritativeClient& operator=(const AuthoritativeClient&) = delete;

    bool connect(Endpoint server, std::string player_name, std::uint64_t nonce = 0);
    bool connect_via_directory(Endpoint directory, std::uint64_t server_id, std::string player_name,
                               std::uint64_t nonce = 0);
    void disconnect();
    ClientUpdateStats update();
    bool send_input(InputMessage input);
    bool set_focus(Vec2i focus);
    bool paint(Vec2i target, MaterialId material, std::uint8_t radius = 1);

    [[nodiscard]] ClientConnectionState state() const noexcept;
    [[nodiscard]] bool connected() const noexcept;
    [[nodiscard]] std::uint8_t client_id() const noexcept;
    [[nodiscard]] std::uint16_t local_port() const noexcept;
    [[nodiscard]] const std::optional<WelcomeMessage>& welcome() const noexcept;
    [[nodiscard]] const std::optional<SnapshotMessage>& latest_snapshot() const noexcept;
    [[nodiscard]] const World* replicated_world() const noexcept;
    [[nodiscard]] World* replicated_world() noexcept;
    [[nodiscard]] std::size_t pending_predictions() const noexcept;
    [[nodiscard]] std::uint32_t acknowledged_input_sequence() const noexcept;
    [[nodiscard]] std::uint64_t chunk_hash_mismatches() const noexcept;
    [[nodiscard]] std::string_view last_error() const noexcept;

  private:
    struct PredictedPaint {
        std::uint32_t input_sequence{};
        std::uint32_t created_update{};
        Vec2i target{};
        MaterialId material{MaterialId::Empty};
        std::uint8_t radius{};
    };

    bool begin_connection(std::string player_name, std::uint64_t nonce);
    bool send_hello();
    bool send_directory_join();
    void poll_datagrams(ClientUpdateStats& stats);
    void handle_packet(const Packet& packet, ClientUpdateStats& stats);
    bool send_packet(const Packet& packet, ClientUpdateStats* stats);
    void flush_channel(ClientUpdateStats& stats);
    void drop_acknowledged_predictions(std::uint32_t acknowledged_sequence);
    std::uint32_t reapply_predictions(RectI chunk_rect);
    [[nodiscard]] std::uint32_t next_target_tick() const noexcept;

    UdpSocket socket_;
    Endpoint server_;
    std::optional<Endpoint> directory_;
    ReliableChannel channel_;
    FragmentAssembler fragments_;
    ClientConnectionState state_{ClientConnectionState::Disconnected};
    std::string player_name_;
    std::uint64_t nonce_{};
    std::uint64_t requested_server_id_{};
    std::uint64_t directory_request_id_{};
    std::uint32_t build_id_{1};
    std::uint32_t network_update_{};
    std::uint32_t next_input_sequence_{1};
    std::uint32_t last_server_update_{};
    bool needs_ack_{};
    std::optional<WelcomeMessage> welcome_;
    std::optional<SnapshotMessage> latest_snapshot_;
    std::unique_ptr<World> replicated_world_;
    std::vector<std::uint64_t> chunk_revisions_;
    std::vector<PredictedPaint> predicted_paints_;
    std::uint32_t acknowledged_input_sequence_{};
    std::uint64_t chunk_hash_mismatches_{};
    std::string last_error_;
};

} // namespace meat2d::net
