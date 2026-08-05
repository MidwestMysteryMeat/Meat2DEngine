#include "meat2d/audio/Audio.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace meat2d::audio {

Mixer::Mixer() {
    clips_.reserve(maximum_clips);
    buses_.reserve(maximum_buses);
    commands_.reserve(maximum_commands_per_frame);
    buses_.push_back({.id = master_bus, .gain = 1.0F, .name = "Master"});
}

std::optional<ClipId> Mixer::register_clip(ClipKind kind,
                                           std::string_view asset_path,
                                           std::uint32_t sample_rate,
                                           std::uint16_t channels,
                                           std::uint64_t frame_count,
                                           std::uint64_t encoded_bytes) {
    if (clips_.size() >= maximum_clips || asset_path.empty() ||
        asset_path.size() > maximum_asset_path_bytes || sample_rate == 0U || channels == 0U ||
        channels > 8U || frame_count == 0U || encoded_bytes == 0U || next_clip_ == invalid_clip) {
        return std::nullopt;
    }
    try {
        ClipInfo clip{};
        clip.id = next_clip_++;
        clip.kind = kind;
        clip.sample_rate = sample_rate;
        clip.channels = channels;
        clip.frame_count = frame_count;
        clip.encoded_bytes = encoded_bytes;
        clip.asset_path.assign(asset_path.begin(), asset_path.end());
        clips_.push_back(std::move(clip));
        return clips_.back().id;
    } catch (...) {
        return std::nullopt;
    }
}

bool Mixer::unregister_clip(ClipId clip) noexcept {
    const auto iterator = std::find_if(clips_.begin(), clips_.end(), [clip](const ClipInfo& item) {
        return item.id == clip;
    });
    if (iterator == clips_.end()) {
        return false;
    }
    clips_.erase(iterator);
    return true;
}

std::span<const ClipInfo> Mixer::clips() const noexcept {
    return clips_;
}

std::optional<BusId> Mixer::create_bus(std::string_view name, float gain) {
    if (buses_.size() >= maximum_buses || name.empty() || name.size() > maximum_bus_name_bytes ||
        !valid_gain(gain) || next_bus_ == invalid_bus) {
        return std::nullopt;
    }
    try {
        BusInfo bus{};
        bus.id = next_bus_++;
        bus.gain = gain;
        bus.name.assign(name.begin(), name.end());
        buses_.push_back(std::move(bus));
        return buses_.back().id;
    } catch (...) {
        return std::nullopt;
    }
}

bool Mixer::set_bus_gain(BusId bus, float gain) noexcept {
    auto* target = find_bus(bus);
    if (target == nullptr || !valid_gain(gain)) {
        return false;
    }
    if (!append({.type = CommandType::SetBusGain, .bus = bus, .value = gain})) {
        return false;
    }
    target->gain = gain;
    return true;
}

std::span<const BusInfo> Mixer::buses() const noexcept {
    return buses_;
}

std::optional<VoiceId> Mixer::play(ClipId clip, PlayOptions options) {
    if (find_clip(clip) == nullptr || find_bus(options.bus) == nullptr ||
        !valid_play_options(options) || next_voice_ == invalid_voice) {
        return std::nullopt;
    }
    const auto voice = next_voice_;
    if (!append({.type = CommandType::Play,
                 .clip = clip,
                 .bus = options.bus,
                 .voice = voice,
                 .value = options.gain,
                 .pitch = options.pitch,
                 .pan = options.pan,
                 .priority = options.priority,
                 .loop = options.loop})) {
        return std::nullopt;
    }
    ++next_voice_;
    return voice;
}

bool Mixer::stop(VoiceId voice) noexcept {
    return voice != invalid_voice && append({.type = CommandType::Stop, .voice = voice});
}

bool Mixer::pause(VoiceId voice) noexcept {
    return voice != invalid_voice && append({.type = CommandType::Pause, .voice = voice});
}

bool Mixer::resume(VoiceId voice) noexcept {
    return voice != invalid_voice && append({.type = CommandType::Resume, .voice = voice});
}

bool Mixer::stop_all(BusId bus) noexcept {
    return bus == invalid_bus || find_bus(bus) != nullptr
               ? append({.type = CommandType::StopAll, .bus = bus})
               : false;
}

bool Mixer::reset_device() noexcept {
    return append({.type = CommandType::ResetDevice});
}

void Mixer::begin_frame() noexcept {
    commands_.clear();
}

std::span<const Command> Mixer::commands() const noexcept {
    return commands_;
}

const ClipInfo* Mixer::find_clip(ClipId clip) const noexcept {
    const auto iterator = std::find_if(clips_.begin(), clips_.end(), [clip](const ClipInfo& item) {
        return item.id == clip;
    });
    return iterator == clips_.end() ? nullptr : &*iterator;
}

BusInfo* Mixer::find_bus(BusId bus) noexcept {
    const auto iterator = std::find_if(buses_.begin(), buses_.end(), [bus](const BusInfo& item) {
        return item.id == bus;
    });
    return iterator == buses_.end() ? nullptr : &*iterator;
}

const BusInfo* Mixer::find_bus(BusId bus) const noexcept {
    const auto iterator = std::find_if(buses_.begin(), buses_.end(), [bus](const BusInfo& item) {
        return item.id == bus;
    });
    return iterator == buses_.end() ? nullptr : &*iterator;
}

bool Mixer::append(Command command) noexcept {
    if (commands_.size() >= maximum_commands_per_frame) {
        return false;
    }
    try {
        command.sequence = next_sequence_;
        commands_.push_back(command);
        ++next_sequence_;
        return true;
    } catch (...) {
        return false;
    }
}

bool Mixer::valid_gain(float gain) noexcept {
    return std::isfinite(gain) && gain >= 0.0F && gain <= 4.0F;
}

bool Mixer::valid_play_options(const PlayOptions& options) noexcept {
    return valid_gain(options.gain) && std::isfinite(options.pitch) && options.pitch >= 0.25F &&
           options.pitch <= 4.0F && std::isfinite(options.pan) && options.pan >= -1.0F &&
           options.pan <= 1.0F;
}

} // namespace meat2d::audio
