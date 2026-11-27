// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginModel.h"
#include "PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <hba/core/wav.h>

#include <cmath>
#include <vector>

namespace {
struct JuceGuard {
    JuceGuard() { juce::initialiseJuce_GUI(); }
    ~JuceGuard() { juce::shutdownJuce_GUI(); }
};

JuceGuard& ensureJuce() {
    static JuceGuard guard;
    return guard;
}

float peakAndFinite(const juce::AudioBuffer<float>& buf, bool& allFinite) {
    float peak = 0.0f;
    for (int ch = 0; ch < buf.getNumChannels(); ++ch) {
        const float* p = buf.getReadPointer(ch);
        for (int i = 0; i < buf.getNumSamples(); ++i) {
            if (!std::isfinite(p[i]))
                allFinite = false;
            peak = std::max(peak, std::abs(p[i]));
        }
    }
    return peak;
}
} // namespace

TEST_CASE("Lineage processor loads all factory presets cleanly with finite bounded audio", "[plugin]") {
    ensureJuce();
    ln::PluginProcessor proc;
    proc.prepareToPlay(48000.0, 256);

    const auto entries = proc.presets.entries();
    REQUIRE(entries.size() >= 20);

    juce::AudioBuffer<float> audio(2, 256);
    juce::MidiBuffer midi;

    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        proc.presets.load(i);
        REQUIRE(proc.printStore.waitForIdle(8000));

        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOn(1, 29, 0.92f), 0); // F1

        bool finite = true;
        float maxPeak = 0.0f;
        for (int blk = 0; blk < 24; ++blk) {
            audio.clear();
            proc.processBlock(audio, midi);
            midi.clear();
            maxPeak = std::max(maxPeak, peakAndFinite(audio, finite));
        }

        INFO("Preset " << i << ": " << entries[static_cast<std::size_t>(i)].name);
        CHECK(finite);
        CHECK(maxPeak > 0.05f);
        CHECK(maxPeak <= 1.15f);

        midi.addEvent(juce::MidiMessage::noteOff(1, 29), 0);
        audio.clear();
        proc.processBlock(audio, midi);
    }
}

TEST_CASE("Lineage live PrintStore Gen-2 bake and Gen-3 stack publish lock-free", "[plugin]") {
    ensureJuce();
    ln::PluginProcessor proc;
    proc.prepareToPlay(48000.0, 256);

    proc.triggerPrintSync(false);
    REQUIRE(proc.printStore.generation() == 2);
    REQUIRE(proc.printStore.state() == ln::ui::PrintState::ready);

    // Switch Gen Mix to 100% and play note
    if (auto* p = proc.state.getParameter("genMix"))
        p->setValueNotifyingHost(1.0f);

    juce::AudioBuffer<float> audio(2, 256);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 31, 0.9f), 0); // G1

    bool finite = true;
    float peak = 0.0f;
    for (int blk = 0; blk < 16; ++blk) {
        audio.clear();
        proc.processBlock(audio, midi);
        midi.clear();
        peak = std::max(peak, peakAndFinite(audio, finite));
    }
    CHECK(finite);
    CHECK(peak > 0.1f);
    CHECK(proc.telemetry.read().hasPrint);
    CHECK(proc.telemetry.read().generation == 2);

    // Stack Gen 3, then test non-destructive generation Step-Back!
    proc.triggerPrintSync(true);
    CHECK(proc.printStore.generation() == 3);
    REQUIRE(proc.printStore.canStepBack());

    REQUIRE(proc.printStore.stepBack());
    CHECK(proc.printStore.generation() == 2);
    CHECK_FALSE(proc.printStore.canStepBack());
}

TEST_CASE("Lineage state round-trip, Undo/Redo and A/B history persist", "[plugin]") {
    ensureJuce();
    ln::PluginProcessor proc;
    proc.prepareToPlay(48000.0, 256);
    ln::PluginModel model(proc);

    const float origHz = model.get(ln::ParamId::filt1Hz);
    model.begin(ln::ParamId::filt1Hz);
    model.set(ln::ParamId::filt1Hz, 2400.0f);
    model.end(ln::ParamId::filt1Hz);
    REQUIRE(model.get(ln::ParamId::filt1Hz) == Catch::Approx(2400.0f).margin(5.0f));
    REQUIRE(model.canUndo());

    model.undo();
    CHECK(model.get(ln::ParamId::filt1Hz) == Catch::Approx(origHz).margin(5.0f));
    REQUIRE(model.canRedo());

    model.redo();
    CHECK(model.get(ln::ParamId::filt1Hz) == Catch::Approx(2400.0f).margin(5.0f));

    // A/B compare
    model.toggleAB();
    CHECK(model.isSlotB());
    model.begin(ln::ParamId::drive);
    model.set(ln::ParamId::drive, 0.85f);
    model.end(ln::ParamId::drive);
    model.toggleAB();
    CHECK_FALSE(model.isSlotB());

    // State round-trip
    juce::MemoryBlock blob;
    proc.getStateInformation(blob);
    REQUIRE(blob.getSize() > 0);

    ln::PluginProcessor restored;
    restored.prepareToPlay(48000.0, 256);
    restored.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    CHECK(restored.currentParams()[ln::ParamId::filt1Hz] == Catch::Approx(2400.0f).margin(5.0f));
}

TEST_CASE("Lineage host session retains exact stacked PCM and clears former step-back history", "[plugin][recall]") {
    ensureJuce();
    ln::PluginProcessor proc;
    proc.prepareToPlay(48000, 64);
    proc.triggerPrintSync(false);
    proc.triggerPrintSync(true);
    const auto expected = proc.printStore.copyBuffer();
    REQUIRE(expected.generation == 3);
    juce::MemoryBlock saved;
    proc.getStateInformation(saved);
    ln::PluginProcessor restored;
    restored.prepareToPlay(44100, 256);
    restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    REQUIRE(restored.printStore.waitForIdle());
    const auto actual = restored.printStore.copyBuffer();
    CHECK(actual.generation == expected.generation);
    CHECK(actual.length == expected.length);
    CHECK(actual.sampleRate == expected.sampleRate);
    CHECK(actual.rootMidiNote == expected.rootMidiNote);
    CHECK(actual.label == expected.label);
    CHECK(bool(actual.left == expected.left));
    CHECK(bool(actual.right == expected.right));
    CHECK_FALSE(restored.printStore.canStepBack());
    ln::PluginModel model(proc);
    model.toggleAB();
    proc.printStore.clear();
    model.toggleAB();
    CHECK(proc.printStore.generation() == 3);
    model.toggleAB();
    CHECK_FALSE(proc.printStore.hasPrint());
}

TEST_CASE("Lineage recalls imported stereo audio without the file and prints four full bars", "[plugin][recall]") {
    ensureJuce();
    ln::PluginProcessor source;
    juce::TemporaryFile wav(".wav");
    std::array<float, 512> samples{};
    samples[96] = -0.5f;
    samples[145] = 0.25f;
    REQUIRE(hba::core::writeWavFloat(wav.getFile().getFullPathName().toStdString(), samples, 2, 44100));
    REQUIRE(source.printStore.loadAudioFile(wav.getFile(), 48000));
    juce::MemoryBlock state;
    source.getStateInformation(state);
    REQUIRE(wav.getFile().deleteFile());
    ln::PluginProcessor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    const auto actual = restored.printStore.copyBuffer();
    const auto expected = source.printStore.copyBuffer();
    CHECK(bool(actual.left == expected.left));
    CHECK(bool(actual.right == expected.right));
    CHECK(actual.label == expected.label);

    auto params = source.currentParams();
    params[ln::ParamId::printBars] = 2;
    source.printStore.renderSync(params, source.curves.all(), 48000, 120, 29, false);
    CHECK(source.printStore.copyBuffer().length == 384000);
}

TEST_CASE("lineage renders a mono host block beyond the prepared size", "[plugin][oversized]") {
    juce::ScopedJuceInitialiser_GUI gui;
    ln::PluginProcessor proc;
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
