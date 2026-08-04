#include "meat2d/net/PacketCodec.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::uint8_t> bytes(data, size);
    const auto packet = meat2d::net::decode_packet(bytes);
    if (!packet) {
        return 0;
    }

    switch (packet->header.type) {
    case meat2d::net::PacketType::Hello:
        static_cast<void>(meat2d::net::decode_hello(packet->payload));
        break;
    case meat2d::net::PacketType::Welcome:
        static_cast<void>(meat2d::net::decode_welcome(packet->payload));
        break;
    case meat2d::net::PacketType::Input:
        static_cast<void>(meat2d::net::decode_input(packet->payload));
        break;
    case meat2d::net::PacketType::Snapshot:
        static_cast<void>(meat2d::net::decode_snapshot(packet->payload));
        break;
    case meat2d::net::PacketType::SceneSnapshot:
        static_cast<void>(meat2d::net::decode_scene_snapshot(packet->payload));
        break;
    default:
        break;
    }
    return 0;
}

