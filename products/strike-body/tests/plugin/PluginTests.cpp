// SPDX-License-Identifier: AGPL-3.0-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "PluginModel.h"
#include "PluginProcessor.h"

#include <cmath>

using Catch::Approx;

TEST_CASE("PluginProcessor loads all 24 factory presets and renders audio cleanly", "[plugin]") {
    sb::PluginProcessor proc;
    proc.prepareToPlay(48000.0, 256);

    const auto list = proc.presets.entries();
    REQUIRE(list.size() >= 24);

    juce::AudioBuffer<float> audio(2, 256);
    juce::MidiBuffer midi;

    for (int idx = 0; idx < static_cast<int>(list.size()); ++idx) {
        proc.presets.load(idx);
        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOn(1, 36, static_cast<juce::uint8>(120)), 0);
        audio.clear();

        double energy = 0.0;
        for (int b = 0; b < 16; ++b) {
            proc.processBlock(audio, midi);
            midi.clear();
            for (int ch = 0; ch < 2; ++ch) {
                for (int i = 0; i < 256; ++i) {
                    const float s = audio.getSample(ch, i);
                    REQUIRE(std::isfinite(s));
                    energy += static_cast<double>(s * s);
                }
            }
        }
        REQUIRE(energy > 0.02);
    }
}

TEST_CASE("PluginProcessor state serialization recalls parameters, macros, and preset name", "[plugin][recall]") {
    sb::PluginProcessor a;
    a.prepareToPlay(48000.0, 256);
    a.presets.load(5);

    if (auto* p = a.state.getParameter("macroPunch"))
        p->setValueNotifyingHost(p->convertTo0to1(62.0f));
    if (auto* p = a.state.getParameter("roomAmt"))
        p->setValueNotifyingHost(p->convertTo0to1(35.0f));

    juce::MemoryBlock saved;
    a.getStateInformation(saved);
    REQUIRE(saved.getSize() > 32);

    sb::PluginProcessor b;
    b.prepareToPlay(48000.0, 256);
    b.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));

    REQUIRE(b.presets.currentName() == a.presets.currentName());
    REQUIRE(b.currentParams().real(sb::ParamId::macroPunch) == Approx(62.0f).margin(0.5f));
    REQUIRE(b.currentParams().real(sb::ParamId::roomAmt) == Approx(35.0f).margin(0.5f));
}

TEST_CASE("PluginModel Undo/Redo, A/B, Archetypes, Mutate, and Auto-Align work end-to-end", "[plugin][model]") {
    sb::PluginProcessor proc;
    proc.prepareToPlay(48000.0, 256);
    sb::PluginModel model(proc);

    const float initialTension = model.get(sb::ParamId::shellTension);
    model.begin(sb::ParamId::shellTension);
    model.set(sb::ParamId::shellTension, 84.0f);
    model.end(sb::ParamId::shellTension);
    REQUIRE(model.get(sb::ParamId::shellTension) == Approx(84.0f).margin(0.5f));
    REQUIRE(model.canUndo());

    model.undo();
    REQUIRE(model.get(sb::ParamId::shellTension) == Approx(initialTension).margin(0.5f));
    REQUIRE(model.canRedo());

    model.redo();
    REQUIRE(model.get(sb::ParamId::shellTension) == Approx(84.0f).margin(0.5f));

    // A/B compare
    model.toggleAB();
    REQUIRE(model.isSlotB());
    model.toggleAB();
    REQUIRE(!model.isSlotB());

    // Quick-Start Archetype + Smart Mutate + Undo
    model.loadArchetype(sb::ui::Archetype::clap);
    REQUIRE(model.presetName() == "Init Layered Clap");
    model.mutateSound();
    REQUIRE(model.presetName().find("(Mut)") != std::string::npos);

    // Auto-Align commits parameters & queues a live audition hit
    model.runAutoAlign();

    juce::AudioBuffer<float> audio(2, 256);
    juce::MidiBuffer midi;
    double energy = 0.0;
    for (int b = 0; b < 12; ++b) {
        proc.processBlock(audio, midi);
        for (int i = 0; i < 256; ++i)
            energy += static_cast<double>(audio.getSample(0, i) * audio.getSample(0, i));
    }
    REQUIRE(energy > 0.02);
    model.refreshVisuals();
    REQUIRE(model.visuals().triggerCount >= 1u);
}
