// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

TEST_CASE("Prism Vein plugin loads all factory presets, recalls chord/curves state, and processes stereo + sidechain",
          "[plugin]") {
    pv::PluginProcessor proc;
    REQUIRE(proc.presets.count() >= 24);

    // Verify Audio FX + Optional Sidechain bus layout support.
    juce::AudioProcessor::BusesLayout stereoWithSc;
    stereoWithSc.inputBuses.add(juce::AudioChannelSet::stereo());
    stereoWithSc.inputBuses.add(juce::AudioChannelSet::stereo());
    stereoWithSc.outputBuses.add(juce::AudioChannelSet::stereo());
    CHECK(proc.isBusesLayoutSupported(stereoWithSc));

    juce::AudioProcessor::BusesLayout monoNoSc;
    monoNoSc.inputBuses.add(juce::AudioChannelSet::mono());
    monoNoSc.inputBuses.add(juce::AudioChannelSet::disabled());
    monoNoSc.outputBuses.add(juce::AudioChannelSet::mono());
    CHECK(proc.isBusesLayoutSupported(monoNoSc));

    proc.prepareToPlay(48000.0, 256);
    CHECK(proc.getLatencySamples() == pv::Engine::kFftSize);

    // Load every factory preset and process a stereo audio block with live MIDI chord input.
    juce::AudioBuffer<float> buf(2, 256);
    juce::MidiBuffer midi;
    for (int i = 0; i < proc.presets.count(); ++i) {
        proc.presets.load(i);
        for (int ch = 0; ch < 2; ++ch) {
            auto* ptr = buf.getWritePointer(ch);
            for (int s = 0; s < 256; ++s)
                ptr[s] = static_cast<float>(0.35 * std::sin(hba::dsp::kTwoPi * 110.0 * s / 48000.0));
        }
        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOn(1, 53, 0.85f), 0);
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.85f), 0);
        proc.processBlock(buf, midi);

        for (int ch = 0; ch < 2; ++ch) {
            const auto* ptr = buf.getReadPointer(ch);
            for (int s = 0; s < 256; ++s)
                REQUIRE(std::isfinite(ptr[s]));
        }
    }

    // Full state round-trip preserves parameters, custom chordMask, and drawn lane curve.
    proc.stateStore.setChordMask(0x059Au);
    proc.stateStore.setLaneCurve(1, hba::dsp::curves::square());
    if (auto* p = proc.state.getParameter("tame"))
        p->setValueNotifyingHost(0.73f);

    juce::MemoryBlock blob;
    proc.getStateInformation(blob);
    REQUIRE(blob.getSize() > 0);

    pv::PluginProcessor restored;
    restored.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    CHECK(restored.stateStore.chordMask() == 0x059Au);
    CHECK(restored.stateStore.laneCurve(1) == hba::dsp::curves::square());
    CHECK(restored.currentParams().real(pv::ParamId::tame) == Catch::Approx(0.73f).margin(0.01f));
}
