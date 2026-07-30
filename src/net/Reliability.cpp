#include "meat2d/net/Reliability.hpp"

#include <algorithm>

namespace meat2d::net {

bool sequence_more_recent(std::uint32_t first, std::uint32_t second) noexcept {
    return first != second &&
           static_cast<std::int32_t>(first - second) > 0;
}

bool sequence_acknowledged(
    std::uint32_t sequence,
    std::uint32_t acknowledgement,
    std::uint32_t acknowledgement_bits) noexcept {
    if (sequence == acknowledgement) {
        return true;
    }
    if (sequence_more_recent(sequence, acknowledgement)) {
        return false;
    }
    const auto distance = acknowledgement - sequence;
    return distance >= 1U && distance <= 32U &&
           (acknowledgement_bits & (1U << (distance - 1U))) != 0U;
}

bool AcknowledgementTracker::observe(std::uint32_t sequence) noexcept {
    if (!initialized_) {
        initialized_ = true;
        acknowledgement_ = sequence;
        acknowledgement_bits_ = 0;
        return true;
    }
    if (sequence == acknowledgement_) {
        return false;
    }
    if (sequence_more_recent(sequence, acknowledgement_)) {
        const auto shift = sequence - acknowledgement_;
        if (shift > 32U) {
            acknowledgement_bits_ = 0;
        } else {
            acknowledgement_bits_ =
                (acknowledgement_bits_ << shift) | (1U << (shift - 1U));
        }
        acknowledgement_ = sequence;
        return true;
    }

    const auto distance = acknowledgement_ - sequence;
    if (distance == 0U || distance > 32U) {
        return false;
    }
    const auto bit = 1U << (distance - 1U);
    if ((acknowledgement_bits_ & bit) != 0U) {
        return false;
    }
    acknowledgement_bits_ |= bit;
    return true;
}

bool AcknowledgementTracker::initialized() const noexcept {
    return initialized_;
}

std::uint32_t AcknowledgementTracker::acknowledgement() const noexcept {
    return acknowledgement_;
}

std::uint32_t AcknowledgementTracker::acknowledgement_bits() const noexcept {
    return acknowledgement_bits_;
}

ReliableChannel::ReliableChannel(ReliabilityConfig config) : config_(config) {
    pending_.reserve(64);
}

Packet ReliableChannel::make_packet(
    PacketType type,
    std::span<const std::uint8_t> payload,
    std::uint32_t update,
    std::uint32_t server_tick,
    bool reliable,
    std::uint8_t additional_flags) {
    Packet packet{};
    packet.header.type = type;
    packet.header.sequence = next_sequence_++;
    packet.header.flags = static_cast<std::uint8_t>(
        additional_flags | (reliable ? PacketFlagReliable : PacketFlagNone));
    packet.payload.assign(payload.begin(), payload.end());
    stamp_acknowledgements(packet.header, server_tick);
    ++stats_.packets_created;

    if (reliable && pending_.size() < config_.maximum_pending_packets) {
        pending_.push_back({
            .packet = packet,
            .last_sent_update = update,
            .attempts = 1,
        });
    }
    return packet;
}

Packet ReliableChannel::make_acknowledgement(
    std::uint32_t update,
    std::uint32_t server_tick) {
    return make_packet(
        PacketType::Acknowledgement,
        {},
        update,
        server_tick,
        false,
        PacketFlagNone);
}

bool ReliableChannel::receive(const PacketHeader& header) {
    apply_acknowledgements(header);
    const bool fresh = received_.observe(header.sequence);
    if (!fresh) {
        ++stats_.duplicates_received;
    }
    return fresh;
}

std::vector<Packet> ReliableChannel::collect_retransmissions(
    std::uint32_t update,
    std::uint32_t server_tick) {
    std::vector<Packet> packets;
    auto pending = pending_.begin();
    while (pending != pending_.end()) {
        if (update - pending->last_sent_update < config_.resend_after_updates) {
            ++pending;
            continue;
        }
        if (pending->attempts >= config_.maximum_attempts) {
            pending = pending_.erase(pending);
            ++stats_.expired_packets;
            continue;
        }

        pending->last_sent_update = update;
        ++pending->attempts;
        stamp_acknowledgements(pending->packet.header, server_tick);
        packets.push_back(pending->packet);
        ++stats_.retransmissions;
        ++pending;
    }
    return packets;
}

std::size_t ReliableChannel::pending_packets() const noexcept {
    return pending_.size();
}

const ReliabilityStats& ReliableChannel::stats() const noexcept {
    return stats_;
}

void ReliableChannel::apply_acknowledgements(const PacketHeader& header) {
    const auto previous_size = pending_.size();
    pending_.erase(
        std::remove_if(
            pending_.begin(),
            pending_.end(),
            [&](const PendingPacket& pending) {
                return sequence_acknowledged(
                    pending.packet.header.sequence,
                    header.acknowledgement,
                    header.acknowledgement_bits);
            }),
        pending_.end());
    stats_.reliable_acked += previous_size - pending_.size();
}

void ReliableChannel::stamp_acknowledgements(
    PacketHeader& header,
    std::uint32_t server_tick) const noexcept {
    header.server_tick = server_tick;
    header.acknowledgement =
        received_.initialized() ? received_.acknowledgement() : 0U;
    header.acknowledgement_bits =
        received_.initialized() ? received_.acknowledgement_bits() : 0U;
}

} // namespace meat2d::net
