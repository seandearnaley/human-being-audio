// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

TEST_CASE(
    "Prism Vein plugin loads all factory presets, recalls chord/curves/latency state, and handles MIDI expression",
    "[plugin]") {
    pv::PluginProcessor proc;
    REQUIRE(proc.presets.count() >= 28);

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

    // Load every factory preset and process a stereo audio block with live MIDI expression.
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
        midi.addEvent(juce::MidiMessage::controllerEvent(1, 64, 127), 8); // Sustain pedal down
        midi.addEvent(juce::MidiMessage::pitchWheel(1, 12288), 16);       // Pitch bend up
        midi.addEvent(juce::MidiMessage::controllerEvent(1, 1, 96), 24);  // Mod wheel
        proc.processBlock(buf, midi);

        for (int ch = 0; ch < 2; ++ch) {
            const auto* ptr = buf.getReadPointer(ch);
            for (int s = 0; s < 256; ++s)
                REQUIRE(std::isfinite(ptr[s]));
        }
    }

    // Large host offline block (> 4096 samples) processes cleanly without overflow.
    juce::AudioBuffer<float> hugeBuf(2, 8192);
    hugeBuf.clear();
    midi.clear();
    proc.processBlock(hugeBuf, midi);
    for (int s = 0; s < 8192; ++s) {
        REQUIRE(std::isfinite(hugeBuf.getSample(0, s)));
        REQUIRE(std::isfinite(hugeBuf.getSample(1, s)));
    }

    // Full state round-trip preserves parameters, custom chordMask, drawn lane curve, and Live 0ms latency!
    proc.stateStore.setChordMask(0x059Au);
    proc.stateStore.setLaneCurve(1, hba::dsp::curves::square());
    if (auto* p = proc.state.getParameter("tame"))
        p->setValueNotifyingHost(0.73f);
    if (auto* p = proc.state.getParameter("latencyMode"))
        p->setValueNotifyingHost(1.0f); // Live 0ms

    juce::MemoryBlock blob;
    proc.getStateInformation(blob);
    REQUIRE(blob.getSize() > 0);

    pv::PluginProcessor restored;
    restored.prepareToPlay(48000.0, 256);
    restored.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    CHECK(restored.stateStore.chordMask() == 0x059Au);
    CHECK(restored.stateStore.laneCurve(1) == hba::dsp::curves::square());
    CHECK(restored.currentParams().real(pv::ParamId::tame) == Catch::Approx(0.73f).margin(0.01f));
    CHECK(restored.getLatencySamples() == 0);
}
