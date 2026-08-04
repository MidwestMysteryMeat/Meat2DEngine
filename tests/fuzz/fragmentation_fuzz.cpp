#include "meat2d/net/Fragmentation.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::span<const std::uint8_t> bytes(data, size);
    static_cast<void>(meat2d::net::decode_fragment(bytes));

    meat2d::net::FragmentAssembler assembler;
    static_cast<void>(assembler.accept(bytes, 1U));
    assembler.expire(601U);
    return 0;
}

