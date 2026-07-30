#pragma once

#include "meat2d/net/PacketCodec.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace meat2d::net {

[[nodiscard]] bool sequence_more_recent(
    std::uint32_t first,
    std::uint32_t second) noexcept;
[[nodiscard]] bool sequence_acknowledged(
    std::uint32_t sequence,
    std::uint32_t acknowledgement,
    std::uint32_t acknowledgement_bits) noexcept;

class AcknowledgementTracker {
  public:
    bool observe(std::uint32_t sequence) noexcept;
    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] std::uint32_t acknowledgement() const noexcept;
    [[nodiscard]] std::uint32_t acknowledgement_bits() const noexcept;

  private:
    bool initialized_{};
    std::uint32_t acknowledgement_{};
    std::uint32_t acknowledgement_bits_{};
};

struct ReliabilityConfig {
    std::uint32_t resend_after_updates{6};
    std::uint8_t maximum_attempts{12};
    std::size_t maximum_pending_packets{1'024};
};

struct ReliabilityStats {
    std::uint64_t packets_created{};
    std::uint64_t reliable_acked{};
    std::uint64_t duplicates_received{};
    std::uint64_t retransmissions{};
    std::uint64_t expired_packets{};
};

class ReliableChannel {
  public:
    explicit ReliableChannel(ReliabilityConfig config = {});

    Packet make_packet(
        PacketType type,
        std::span<const std::uint8_t> payload,
        std::uint32_t update,
        std::uint32_t server_tick,
        bool reliable,
        std::uint8_t additional_flags = PacketFlagNone);
    Packet make_acknowledgement(std::uint32_t update, std::uint32_t server_tick);
    bool receive(const PacketHeader& header);
    std::vector<Packet> collect_retransmissions(
        std::uint32_t update,
        std::uint32_t server_tick);

    [[nodiscard]] std::size_t pending_packets() const noexcept;
    [[nodiscard]] const ReliabilityStats& stats() const noexcept;

  private:
    struct PendingPacket {
        Packet packet;
        std::uint32_t last_sent_update{};
        std::uint8_t attempts{};
    };

    void apply_acknowledgements(const PacketHeader& header);
    void stamp_acknowledgements(PacketHeader& header, std::uint32_t server_tick) const noexcept;

    ReliabilityConfig config_;
    AcknowledgementTracker received_;
    std::vector<PendingPacket> pending_;
    std::uint32_t next_sequence_{1};
    ReliabilityStats stats_{};
};

} // namespace meat2d::net
