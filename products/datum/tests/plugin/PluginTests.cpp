// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginModel.h"
#include "PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

using Catch::Approx;

TEST_CASE("Datum plugin exposes canonical parameter count, sidechain bus, and factory presets", "[plugin]") {
    dt::PluginProcessor proc;

    CHECK(proc.getParameters().size() == static_cast<int>(dt::kParamCount));
    CHECK(proc.presets.count() == 32);

    // VST3 Aux sidechain extension reports no main audio input
    auto* vst3Ext = proc.getVST3ClientExtensions();
    REQUIRE(vst3Ext != nullptr);
    CHECK_FALSE(vst3Ext->getPluginHasMainInput());

    // Accepts stereo output with disabled or stereo sidechain input
    juce::AudioProcessor::BusesLayout disabledSc;
    disabledSc.outputBuses.add(juce::AudioChannelSet::stereo());
    disabledSc.inputBuses.add(juce::AudioChannelSet::disabled());
    CHECK(proc.isBusesLayoutSupported(disabledSc));

    juce::AudioProcessor::BusesLayout stereoSc;
    stereoSc.outputBuses.add(juce::AudioChannelSet::stereo());
    stereoSc.inputBuses.add(juce::AudioChannelSet::stereo());
    CHECK(proc.isBusesLayoutSupported(stereoSc));
}

TEST_CASE("Datum plugin loads all 32 presets and recalls full state including splines & pattern", "[plugin]") {
    dt::PluginProcessor proc;
    proc.prepareToPlay(48000.0, 256);

    const int nPresets = proc.presets.count();
    REQUIRE(nPresets == 32);

    for (int i = 0; i < nPresets; ++i) {
        proc.presets.load(i);
        juce::AudioBuffer<float> buf(2, 256);
        buf.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 36, 0.95f), 0);
        proc.processBlock(buf, midi);

        float maxAbs = 0.0f;
        for (int ch = 0; ch < 2; ++ch) {
            for (int n = 0; n < 256; ++n) {
                const float s = buf.getSample(ch, n);
                REQUIRE(std::isfinite(s));
                maxAbs = std::max(maxAbs, std::abs(s));
            }
        }
        CHECK(maxAbs > 0.05f);
    }

    // Mutate state + splines + pattern, save, and verify exact recall in fresh instance
    proc.presets.load(3);
    auto pitch = proc.stateStore.pitchCurve();
    hba::dsp::insertEnvelopeNode(pitch, 0.14f, 0.48f, -0.55f, hba::dsp::EnvSegment::exp);
    proc.stateStore.setPitchCurve(pitch);

    auto pat = proc.stateStore.pattern();
    pat.slots[1].tuneOffsetSt = -2.5f;
    pat.steps[5] = {102, 2, 3};
    proc.stateStore.setPattern(pat);

    juce::MemoryBlock blob;
    proc.getStateInformation(blob);
    REQUIRE(blob.getSize() > 64);

    dt::PluginProcessor restored;
    restored.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));

    CHECK(restored.presets.currentName() == proc.presets.currentName());
    CHECK(restored.stateStore.pitchCurve() == pitch);
    CHECK(restored.stateStore.pattern() == pat);
    CHECK(restored.currentParams()[dt::ParamId::tuneHz] ==
          Approx(proc.currentParams()[dt::ParamId::tuneHz]).margin(0.05f));
}

TEST_CASE("Datum plugin processes live sidechain without corrupting kick output", "[plugin]") {
    dt::PluginProcessor proc;
    juce::AudioProcessor::BusesLayout stereoSc;
    stereoSc.outputBuses.add(juce::AudioChannelSet::stereo());
    stereoSc.inputBuses.add(juce::AudioChannelSet::stereo());
    REQUIRE(proc.setBusesLayout(stereoSc));
    proc.prepareToPlay(48000.0, 512);

    const double fs = 48000.0;
    const double bassHz = 55.0;
    double ph = 0.0;

    for (int block = 0; block < 24; ++block) {
        juce::AudioBuffer<float> buf(2, 512);
        for (int n = 0; n < 512; ++n) {
            const float s = 0.55f * static_cast<float>(std::sin(2.0 * std::numbers::pi * ph));
            ph += bassHz / fs;
            buf.setSample(0, n, s);
            buf.setSample(1, n, s);
        }
        juce::MidiBuffer midi;
        if (block == 2)
            midi.addEvent(juce::MidiMessage::noteOn(1, 36, 0.95f), 0);
        proc.processBlock(buf, midi);
    }

    const auto tel = proc.telemetry.read();
    CHECK(tel.scActive);
    CHECK(tel.scRootHz == Approx(55.0f).margin(4.0f));
}

TEST_CASE("datum renders a mono host block beyond the prepared size", "[plugin][oversized]") {
    juce::ScopedJuceInitialiser_GUI gui;
    dt::PluginProcessor proc;
    auto layout = proc.getBusesLayout();
    layout.outputBuses.set(0, juce::AudioChannelSet::mono());
    REQUIRE(proc.setBusesLayout(layout));
    proc.prepareToPlay(48000, 64);
    juce::AudioBuffer<float> audio(1, 8192);
    audio.clear();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, 0.95f), 4096);
    midi.addEvent(juce::MidiMessage::noteOff(1, 36), 7800);
    proc.processBlock(audio, midi);
    CHECK(audio.getMagnitude(4096, 4096) > 0.001f);
}

TEST_CASE("Datum retains its aliased stereo sidechain throughout a large callback", "[plugin][oversized]") {
    juce::ScopedJuceInitialiser_GUI gui;
    dt::PluginProcessor proc;
    auto layout = proc.getBusesLayout();
    layout.inputBuses.set(0, juce::AudioChannelSet::stereo());
    REQUIRE(proc.setBusesLayout(layout));
    proc.prepareToPlay(48000, 64);
    juce::AudioBuffer<float> audio(2, 8192);
    for (int i = 0; i < audio.getNumSamples(); ++i) {
        const auto bass = static_cast<float>(0.55 * std::sin(i * 55.0 * 2.0 * std::numbers::pi / 48000.0));
        audio.setSample(0, i, bass);
        audio.setSample(1, i, bass);
    }
    juce::MidiBuffer midi;
    proc.processBlock(audio, midi);
    CHECK(proc.telemetry.read().scActive);
    CHECK(proc.telemetry.read().scRootHz == Approx(55.0f).margin(4.0f));
}
