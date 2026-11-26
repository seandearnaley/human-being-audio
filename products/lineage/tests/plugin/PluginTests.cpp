// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginModel.h"
#include "PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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

    // Stack Gen 3
    proc.triggerPrintSync(true);
    CHECK(proc.printStore.generation() == 3);
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
