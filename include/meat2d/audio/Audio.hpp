#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace meat2d::audio {

using ClipId = std::uint32_t;
using BusId = std::uint16_t;
using VoiceId = std::uint32_t;

inline constexpr ClipId invalid_clip = 0;
inline constexpr BusId invalid_bus = 0;
inline constexpr VoiceId invalid_voice = 0;
inline constexpr BusId master_bus = 1;
inline constexpr std::size_t maximum_clips = 4096U;
inline constexpr std::size_t maximum_buses = 32U;
inline constexpr std::size_t maximum_commands_per_frame = 4096U;
inline constexpr std::size_t maximum_asset_path_bytes = 1024U;
inline constexpr std::size_t maximum_bus_name_bytes = 64U;

enum class ClipKind : std::uint8_t { Sample, Stream };
enum class CommandType : std::uint8_t {
    Play,
    Stop,
    Pause,
    Resume,
    StopAll,
    SetBusGain,
    ResetDevice,
};

struct ClipInfo {
    ClipId id{invalid_clip};
    ClipKind kind{ClipKind::Sample};
    std::uint32_t sample_rate{};
    std::uint16_t channels{};
    std::uint64_t frame_count{};
    std::uint64_t encoded_bytes{};
    std::string asset_path;
};

struct BusInfo {
    BusId id{invalid_bus};
    float gain{1.0F};
    std::string name;
};

struct PlayOptions {
    BusId bus{master_bus};
    float gain{1.0F};
    float pitch{1.0F};
    float pan{};
    std::uint8_t priority{};
    bool loop{};
};

struct Command {
    std::uint64_t sequence{};
    CommandType type{CommandType::Play};
    ClipId clip{invalid_clip};
    BusId bus{invalid_bus};
    VoiceId voice{invalid_voice};
    float value{};
    float pitch{1.0F};
    float pan{};
    std::uint8_t priority{};
    bool loop{};
};

class Mixer {
  public:
    Mixer();

    [[nodiscard]] std::optional<ClipId> register_clip(ClipKind kind,
                                                       std::string_view asset_path,
                                                       std::uint32_t sample_rate,
                                                       std::uint16_t channels,
                                                       std::uint64_t frame_count,
                                                       std::uint64_t encoded_bytes);
    bool unregister_clip(ClipId clip) noexcept;
    [[nodiscard]] std::span<const ClipInfo> clips() const noexcept;

    [[nodiscard]] std::optional<BusId> create_bus(std::string_view name, float gain = 1.0F);
    bool set_bus_gain(BusId bus, float gain) noexcept;
    [[nodiscard]] std::span<const BusInfo> buses() const noexcept;

    [[nodiscard]] std::optional<VoiceId> play(ClipId clip, PlayOptions options = {});
    bool stop(VoiceId voice) noexcept;
    bool pause(VoiceId voice) noexcept;
    bool resume(VoiceId voice) noexcept;
    bool stop_all(BusId bus = invalid_bus) noexcept;
    bool reset_device() noexcept;

    void begin_frame() noexcept;
    [[nodiscard]] std::span<const Command> commands() const noexcept;

  private:
    [[nodiscard]] const ClipInfo* find_clip(ClipId clip) const noexcept;
    [[nodiscard]] BusInfo* find_bus(BusId bus) noexcept;
    [[nodiscard]] const BusInfo* find_bus(BusId bus) const noexcept;
    [[nodiscard]] bool append(Command command) noexcept;
    [[nodiscard]] static bool valid_gain(float gain) noexcept;
    [[nodiscard]] static bool valid_play_options(const PlayOptions& options) noexcept;

    std::vector<ClipInfo> clips_;
    std::vector<BusInfo> buses_;
    std::vector<Command> commands_;
    ClipId next_clip_{1};
    BusId next_bus_{2};
    VoiceId next_voice_{1};
    std::uint64_t next_sequence_{1};
};

} // namespace meat2d::audio
