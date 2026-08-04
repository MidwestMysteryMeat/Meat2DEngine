#include "meat2d/replay/Replay.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    meat2d::replay::ReplayLog log;
    static_cast<void>(log.decode(std::span<const std::uint8_t>(data, size)));
    return 0;
}

