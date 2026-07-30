// Replay inspector: loads a .replay file recorded by the sandbox (F2 to
// start/stop recording, F3 to save), re-simulates it from tick zero, and
// reports whether every recorded checkpoint hash was reproduced. A
// divergence pinpoints the exact tick where the run stopped matching,
// making it a debugging tool for the determinism contract, not just a
// scrub-through-the-footage viewer.

#include "meat2d/core/Version.hpp"
#include "meat2d/replay/Replay.hpp"

#include <charconv>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {

bool parse_ticks(std::string_view text, meat2d::Tick& output) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), output);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: meat2d_replay <file.replay> [ticks]\n";
        return 2;
    }

    std::ifstream file(argv[1], std::ios::binary);
    if (!file) {
        std::cerr << "could not open " << argv[1] << '\n';
        return 1;
    }
    const std::vector<std::uint8_t> bytes(
        (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    meat2d::replay::ReplayLog log;
    if (!log.decode(bytes)) {
        std::cerr << "failed to decode " << argv[1]
                  << " (bad magic, unsupported version, or truncated file)\n";
        return 1;
    }

    meat2d::Tick total_ticks = log.checkpoints().empty() ? 0 : log.checkpoints().back().tick;
    if (argc >= 3) {
        std::string_view ticks_argument(argv[2]);
        if (!parse_ticks(ticks_argument, total_ticks)) {
            std::cerr << "could not parse tick count '" << argv[2] << "'\n";
            return 2;
        }
    }
    if (total_ticks == 0) {
        std::cerr << "no checkpoints in the log and no tick count given on the command line\n";
        return 2;
    }

    std::cout << "Meat2D replay inspector " << meat2d::version_string << '\n'
              << "world " << log.config().width << "x" << log.config().height << " seed "
              << log.config().seed << '\n'
              << log.paint_events().size() << " paint events, " << log.checkpoints().size()
              << " checkpoints, playing " << total_ticks << " ticks\n";

    const auto result = meat2d::replay::play(log, total_ticks);
    if (result.outcome == meat2d::replay::ReplayOutcome::Matched) {
        std::cout << "MATCHED — " << result.ticks_played
                  << " ticks reproduced every recorded checkpoint\n";
        return 0;
    }

    std::cout << "DIVERGED at tick " << result.divergent_tick << '\n'
              << "  expected hash 0x" << std::hex << result.expected_hash << '\n'
              << "  actual hash   0x" << result.actual_hash << std::dec << '\n';
    return 1;
}
