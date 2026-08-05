#include "TestSupport.hpp"

#include "meat2d/audio/Audio.hpp"

#include <cmath>
#include <limits>
#include <string>

namespace meat2d_tests {

void test_audio_mixer() {
    meat2d::audio::Mixer mixer;
    check(mixer.buses().size() == 1U && mixer.buses()[0].id == meat2d::audio::master_bus,
          "audio mixer did not create the master bus");
    const auto effects = mixer.create_bus("Effects", 0.75F);
    check(effects.has_value(), "audio mixer rejected a valid bus");
    check(mixer.set_bus_gain(*effects, 0.5F), "audio mixer rejected a valid bus gain");

    const auto clip = mixer.register_clip(meat2d::audio::ClipKind::Sample,
                                           "audio/ui/click.ogg",
                                           48000U,
                                           2U,
                                           4800U,
                                           2048U);
    check(clip.has_value(), "audio mixer rejected a valid clip");
    const auto voice = mixer.play(*clip,
                                  {.bus = *effects,
                                   .gain = 0.8F,
                                   .pitch = 1.1F,
                                   .pan = -0.25F,
                                   .priority = 3U,
                                   .loop = false});
    check(voice.has_value(), "audio mixer rejected a valid play request");
    check(mixer.pause(*voice) && mixer.resume(*voice) && mixer.stop(*voice),
          "audio mixer rejected voice lifecycle commands");
    check(mixer.stop_all(*effects) && mixer.reset_device(),
          "audio mixer rejected recovery commands");
    check(mixer.commands().size() == 7U, "audio mixer emitted the wrong command count");
    for (std::size_t index = 1; index < mixer.commands().size(); ++index) {
        check(mixer.commands()[index - 1U].sequence < mixer.commands()[index].sequence,
              "audio command sequence is not strictly increasing");
    }
    check(mixer.commands()[0].type == meat2d::audio::CommandType::SetBusGain,
          "audio gain command was not recorded first");
    check(mixer.commands()[1].type == meat2d::audio::CommandType::Play,
          "audio play command was not recorded second");
    check(mixer.commands()[1].clip == *clip && mixer.commands()[1].bus == *effects,
          "audio play command lost its resource routing");

    mixer.begin_frame();
    check(mixer.commands().empty(), "audio frame boundary did not clear commands");
    check(!mixer.play(999U).has_value(), "audio mixer played an unknown clip");
    check(!mixer.create_bus("Bad", std::numeric_limits<float>::quiet_NaN()).has_value(),
          "audio mixer accepted a NaN bus gain");
    check(!mixer.set_bus_gain(*effects, std::numeric_limits<float>::infinity()),
          "audio mixer accepted an infinite bus gain");
    check(!mixer.play(*clip, {.bus = *effects, .pitch = 0.1F}).has_value(),
          "audio mixer accepted an out-of-range pitch");
    check(!mixer.play(*clip, {.bus = *effects, .pan = 2.0F}).has_value(),
          "audio mixer accepted an out-of-range pan");
    check(!mixer.stop(meat2d::audio::invalid_voice), "audio mixer stopped an invalid voice");
    check(!mixer.unregister_clip(999U), "audio mixer removed an unknown clip");
    check(mixer.unregister_clip(*clip), "audio mixer did not remove a registered clip");

    meat2d::audio::Mixer bounded;
    const auto bounded_clip = bounded.register_clip(meat2d::audio::ClipKind::Stream,
                                                     "music/theme.ogg",
                                                     44100U,
                                                     2U,
                                                     1U,
                                                     4096U);
    check(bounded_clip.has_value(), "audio stream registration failed");
    for (std::size_t index = 0; index < meat2d::audio::maximum_commands_per_frame; ++index) {
        check(bounded.stop_all(), "audio command capacity rejected a valid command");
    }
    check(!bounded.reset_device(), "audio command capacity was exceeded");
}

} // namespace meat2d_tests
