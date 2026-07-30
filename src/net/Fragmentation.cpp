#include "meat2d/net/Fragmentation.hpp"

#include "meat2d/net/PacketCodec.hpp"

#include <algorithm>

namespace meat2d::net {

std::vector<std::vector<std::uint8_t>> fragment_payload(
    std::uint32_t message_id,
    std::span<const std::uint8_t> payload) {
    if (payload.size() > maximum_fragmented_message_bytes) {
        return {};
    }
    const auto count = static_cast<std::uint16_t>(std::max<std::size_t>(
        1,
        (payload.size() + maximum_fragment_data_bytes - 1U) /
            maximum_fragment_data_bytes));
    if (count > maximum_fragments_per_message) {
        return {};
    }

    std::vector<std::vector<std::uint8_t>> fragments;
    fragments.reserve(count);
    for (std::uint16_t index = 0; index < count; ++index) {
        const auto offset =
            static_cast<std::size_t>(index) * maximum_fragment_data_bytes;
        const auto size =
            std::min(maximum_fragment_data_bytes, payload.size() - offset);
        ByteWriter writer(maximum_datagram_bytes - encoded_header_bytes);
        writer.write_u32(message_id);
        writer.write_u16(index);
        writer.write_u16(count);
        writer.write_bytes(payload.subspan(offset, size));
        fragments.push_back(writer.take());
    }
    return fragments;
}

std::optional<MessageFragment> decode_fragment(
    std::span<const std::uint8_t> payload) {
    if (payload.size() < fragment_header_bytes ||
        payload.size() > maximum_datagram_bytes - encoded_header_bytes) {
        return std::nullopt;
    }
    MessageFragment fragment{};
    ByteReader reader(payload);
    if (!reader.read_u32(fragment.message_id) ||
        !reader.read_u16(fragment.fragment_index) ||
        !reader.read_u16(fragment.fragment_count) ||
        fragment.fragment_count == 0U ||
        fragment.fragment_count > maximum_fragments_per_message ||
        fragment.fragment_index >= fragment.fragment_count) {
        return std::nullopt;
    }
    std::span<const std::uint8_t> data;
    if (!reader.read_bytes(reader.remaining(), data)) {
        return std::nullopt;
    }
    fragment.data.assign(data.begin(), data.end());
    return fragment;
}

FragmentAssembler::FragmentAssembler(
    std::uint32_t expire_after_updates,
    std::size_t maximum_assemblies)
    : expire_after_updates_(expire_after_updates),
      maximum_assemblies_(maximum_assemblies) {
    assemblies_.reserve(maximum_assemblies);
}

std::optional<std::vector<std::uint8_t>> FragmentAssembler::accept(
    std::span<const std::uint8_t> fragment_payload_bytes,
    std::uint32_t update) {
    const auto fragment = decode_fragment(fragment_payload_bytes);
    if (!fragment) {
        return std::nullopt;
    }

    auto assembly = std::find_if(
        assemblies_.begin(), assemblies_.end(), [&](const Assembly& candidate) {
            return candidate.message_id == fragment->message_id;
        });
    if (assembly == assemblies_.end()) {
        if (assemblies_.size() >= maximum_assemblies_) {
            return std::nullopt;
        }
        Assembly created{};
        created.message_id = fragment->message_id;
        created.fragment_count = fragment->fragment_count;
        created.last_update = update;
        created.fragments.resize(fragment->fragment_count);
        created.received.resize(fragment->fragment_count);
        assemblies_.push_back(std::move(created));
        assembly = assemblies_.end() - 1;
    }
    if (assembly->fragment_count != fragment->fragment_count) {
        return std::nullopt;
    }
    assembly->last_update = update;

    const auto index = static_cast<std::size_t>(fragment->fragment_index);
    if (!assembly->received[index]) {
        if (assembly->total_bytes + fragment->data.size() >
            maximum_fragmented_message_bytes) {
            assemblies_.erase(assembly);
            return std::nullopt;
        }
        assembly->fragments[index] = fragment->data;
        assembly->received[index] = true;
        ++assembly->received_count;
        assembly->total_bytes += fragment->data.size();
    }
    if (assembly->received_count != assembly->fragment_count) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> completed;
    completed.reserve(assembly->total_bytes);
    for (const auto& bytes : assembly->fragments) {
        completed.insert(completed.end(), bytes.begin(), bytes.end());
    }
    assemblies_.erase(assembly);
    return completed;
}

void FragmentAssembler::expire(std::uint32_t update) {
    assemblies_.erase(
        std::remove_if(
            assemblies_.begin(),
            assemblies_.end(),
            [&](const Assembly& assembly) {
                return update - assembly.last_update > expire_after_updates_;
            }),
        assemblies_.end());
}

std::size_t FragmentAssembler::pending_messages() const noexcept {
    return assemblies_.size();
}

} // namespace meat2d::net
