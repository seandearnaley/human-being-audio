// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"
#include "cl/smf.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

namespace {

class FixedPlayHead final : public juce::AudioPlayHead {
  public:
    double bpm = 172.0;
    double ppq = 0.0;
    bool playing = true;

    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo info;
        info.setBpm(bpm);
        info.setPpqPosition(ppq);
        info.setIsPlaying(playing);
        return info;
    }
};

} // namespace

TEST_CASE("Cutline processor loads all 24 presets, generates MIDI + finite audio on uneven blocks, and recalls state",
          "[plugin]") {
    juce::ScopedJuceInitialiser_GUI juceInit;

    cl::PluginProcessor proc;
    proc.prepareToPlay(48000.0, 257);

    REQUIRE(proc.presets.all().size() >= 24);

    FixedPlayHead playHead;
    proc.setPlayHead(&playHead);

    juce::AudioBuffer<float> audio(2, 257);
    juce::MidiBuffer midi;

    int totalMidiOut = 0;
    const double beatsPerBlock = (playHead.bpm / 60.0 / 48000.0) * 257.0;

    for (std::size_t idx = 0; idx < proc.presets.all().size(); ++idx) {
        proc.presets.load(static_cast<int>(idx));
        playHead.ppq = 0.0;
        for (int b = 0; b < 16; ++b) {
            audio.clear();
            midi.clear();
            proc.processBlock(audio, midi);
            totalMidiOut += midi.getNumEvents();
            for (int c = 0; c < 2; ++c) {
                for (int i = 0; i < 257; ++i)
                    REQUIRE(std::isfinite(audio.getSample(c, i)));
            }
            playHead.ppq += beatsPerBlock;
        }
    }
    CHECK(totalMidiOut > 50);

    // Exported SMF parses cleanly through JUCE MidiFile
    cl::Params defaults;
    const auto midFile = proc.exporter.writeTempFile(defaults, proc.curves.all(), 172.0, "TestExport");
    REQUIRE(midFile.existsAsFile());
    {
        juce::FileInputStream stream(midFile);
        REQUIRE(stream.openedOk());
        juce::MidiFile parsed;
        REQUIRE(parsed.readFrom(stream));
        CHECK(parsed.getNumTracks() == 1);
        CHECK(parsed.getTimeFormat() == cl::kSmfPpq);
        CHECK(parsed.getTrack(0)->getNumEvents() > 10);
    }
    midFile.deleteFile();

    // Round-trip state including custom curve
    proc.curves.set(0, cl::curves::rampDown());
    if (auto* param = proc.state.getParameter("gravity"))
        param->setValueNotifyingHost(0.75f);

    juce::MemoryBlock stateBlob;
    proc.getStateInformation(stateBlob);

    cl::PluginProcessor restored;
    restored.prepareToPlay(48000.0, 257);
    restored.setStateInformation(stateBlob.getData(), static_cast<int>(stateBlob.getSize()));

    REQUIRE(restored.curves.get(0) == cl::curves::rampDown());
    REQUIRE(restored.state.getParameter("gravity")->getValue() == Catch::Approx(0.75f).margin(1e-4f));
}
