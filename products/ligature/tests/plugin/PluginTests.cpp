// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_events/juce_events.h>

#include <cmath>
#include <map>
#include <memory>
#include <set>

using namespace lg;

namespace {

struct Init {
    juce::ScopedJuceInitialiser_GUI gui;
};

struct PlayHead final : juce::AudioPlayHead {
    double ppq = 0.0;
    double bpm = 120.0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo info;
        info.setBpm(bpm);
        info.setPpqPosition(ppq);
        info.setIsPlaying(playing);
        return info;
    }
};

struct Host {
    std::unique_ptr<PluginProcessor> processor = std::make_unique<PluginProcessor>();
    PlayHead head;
    juce::AudioBuffer<float> audio{2, 512};
    juce::MidiBuffer midi;
    std::vector<std::pair<int, int>> ons; // channel, pitch

    Host() {
        processor->setPlayHead(&head);
        processor->setRateAndBufferSizeDetails(48000.0, 512);
        processor->prepareToPlay(48000.0, 512);
    }

    void block(const juce::MidiBuffer& in = {}) {
        midi = in;
        processor->processBlock(audio, midi);
        for (const auto m : midi) {
            const auto msg = m.getMessage();
            if (msg.isNoteOn())
                ons.push_back({msg.getChannel(), msg.getNoteNumber()});
        }
        if (head.playing)
            head.ppq += 512.0 * head.bpm / 60.0 / 48000.0;
    }
};

} // namespace

TEST_CASE("The plugin plays every part on its own channel while the host plays", "[plugin]") {
    Init init;
    Host host;
    for (int i = 0; i < 200; ++i)
        host.block();
    std::set<int> channels;
    for (const auto& [ch, pitch] : host.ons)
        channels.insert(ch);
    CHECK(channels == std::set<int>{1, 2, 3, 4});
    CHECK(host.processor->telemetry().hostPlaying);
}

TEST_CASE("The song survives a save and reload", "[plugin]") {
    Init init;
    Host a;
    a.processor->document().cycleStutter(ItemRef::section(1), 9);
    a.processor->document().setStyle(Role::arp, 0);
    a.processor->songChanged();
    juce::MemoryBlock state;
    a.processor->getStateInformation(state);

    Host b;
    b.processor->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    CHECK(b.processor->document().song() == a.processor->document().song());

    b.processor->setStateInformation("nonsense", 8); // unreadable bytes load the default song
    CHECK(b.processor->document().song().sections.size() >= 2);
}

TEST_CASE("MIDI notes C-1 to B-1 launch sections and bridges", "[plugin]") {
    Init init;
    Host host;
    for (int i = 0; i < 4; ++i)
        host.block();
    juce::MidiBuffer in;
    in.addEvent(juce::MidiMessage::noteOn(1, 1, 1.0f), 0); // C#-1: launch B
    host.block(in);
    host.block();
    const auto t = host.processor->telemetry();
    CHECK(t.performance.planActive);
    CHECK(t.performance.planTarget == 1);
}

TEST_CASE("Bypass ends every note", "[plugin]") {
    Init init;
    Host host;
    for (int i = 0; i < 20; ++i)
        host.block();
    juce::MidiBuffer midi;
    host.processor->processBlockBypassed(host.audio, midi);
    host.processor->processBlockBypassed(host.audio, midi);
    int ons = 0;
    for (const auto m : midi)
        ons += m.getMessage().isNoteOn() ? 1 : 0;
    CHECK(ons == 0);
}

TEST_CASE("Factory presets load", "[plugin]") {
    Init init;
    Host host;
    const auto& all = host.processor->presets.all();
    REQUIRE(!all.empty());
    host.processor->loadPreset(static_cast<int>(all.size()) - 1);
    CHECK(host.processor->document().song().name == all.back().name);
    CHECK(host.processor->presetName() == all.back().name);
    CHECK(host.processor->document().canUndo());
}

TEST_CASE("A follower takes the leader's song and plays one part on channel 1", "[plugin]") {
    Init init;
    Host leader;
    Host follower;
    follower.processor->setOutputRole(static_cast<int>(Role::bass));
    leader.processor->document().setSectionKey(0, {2, Mode::minor});
    leader.processor->songChanged();
    leader.processor->serviceMessageThread();
    follower.processor->serviceMessageThread();
    CHECK(follower.processor->document().song().sections[0].key == Key{2, Mode::minor});
    for (int i = 0; i < 100; ++i) {
        leader.block();
        follower.block();
    }
    std::set<int> channels;
    for (const auto& [ch, pitch] : follower.ons)
        channels.insert(ch);
    CHECK(channels == std::set<int>{1});
    std::vector<int> leaderBass, followerBass;
    for (const auto& [ch, pitch] : leader.ons)
        if (ch == 2)
            leaderBass.push_back(pitch);
    for (const auto& [ch, pitch] : follower.ons)
        followerBass.push_back(pitch);
    CHECK(!followerBass.empty());
    CHECK(leaderBass == followerBass);
}

TEST_CASE("Changing Scene while the host is stopped does not start playback", "[plugin]") {
    Init init;
    Host host;
    host.head.playing = false;
    host.block();
    host.processor->setParamValue(ParamId::scene, 2.0f); // B
    for (int i = 0; i < 20; ++i)
        host.block();
    CHECK_FALSE(host.processor->telemetry().previewPlaying);
    CHECK(host.ons.empty());
}

TEST_CASE("A key chord ends on release even after One-key chords is switched off", "[plugin]") {
    Init init;
    Host host;
    host.head.playing = false;
    host.block();
    std::map<std::pair<int, int>, int> open;
    const auto count = [&] {
        for (const auto m : host.midi) {
            const auto msg = m.getMessage();
            if (msg.isNoteOn())
                ++open[{msg.getChannel(), msg.getNoteNumber()}];
            else if (msg.isNoteOff())
                --open[{msg.getChannel(), msg.getNoteNumber()}];
        }
    };
    juce::MidiBuffer press;
    press.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    host.block(press);
    count();
    REQUIRE(!open.empty());
    host.processor->setParamValue(ParamId::keyChords, 0.0f);
    juce::MidiBuffer release;
    release.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
    host.block(release);
    count();
    for (const auto& [note, n] : open)
        CHECK(n == 0);
}

TEST_CASE("Mono output is the average of the two sides", "[plugin]") {
    Init init;
    Host stereo, mono;
    mono.audio.setSize(1, 512);
    double worst = 0.0, loudest = 0.0;
    for (int i = 0; i < 60; ++i) {
        stereo.block();
        mono.block();
        for (int n = 0; n < 512; ++n) {
            const double avg = 0.5 * (stereo.audio.getSample(0, n) + stereo.audio.getSample(1, n));
            worst = std::max(worst, std::abs(avg - mono.audio.getSample(0, n)));
            loudest = std::max(loudest, std::abs(avg));
        }
    }
    CHECK(loudest > 0.01);
    CHECK(worst < 1e-4);
}

TEST_CASE("A looped performance is captured pass after pass, never on top of itself", "[plugin]") {
    Init init;
    Host host;
    while (host.head.ppq < 8.0)
        host.block();
    host.head.ppq = 0.0; // the loop brace sends the host back
    while (host.head.ppq < 8.0)
        host.block();
    host.processor->serviceMessageThread();
    const auto notes = host.processor->capturedPerformance();
    REQUIRE(!notes.empty());
    double last = 0.0;
    for (const auto& n : notes) {
        CHECK(n.endBeats >= n.startBeats);
        last = std::max(last, n.startBeats);
    }
    CHECK(last > 9.0); // the second pass follows the first
}
