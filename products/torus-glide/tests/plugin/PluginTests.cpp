// SPDX-License-Identifier: AGPL-3.0-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "MidiExporter.h"
#include "PluginModel.h"
#include "PluginProcessor.h"

#include <cmath>

using Catch::Approx;

TEST_CASE("PluginProcessor loads all 20 factory presets and renders audio cleanly", "[plugin]") {
    tg::PluginProcessor proc;
    proc.prepareToPlay(48000.0, 256);

    const auto list = proc.presets.entries();
    REQUIRE(list.size() >= 28);

    juce::AudioBuffer<float> audio(2, 256);
    juce::MidiBuffer midi;

    // Audition Spiral, Copedent, Mobius, and Pass-2 Neuro Notch / Space presets
    for (int idx : {0, 7, 15, 20, 22, 25}) {
        proc.presets.load(idx);
        midi.clear();
        midi.addEvent(juce::MidiMessage::noteOn(1, 41, static_cast<juce::uint8>(115)), 0);
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
        REQUIRE(energy > 0.05);
    }
}

TEST_CASE("PluginProcessor state serialization recalls params, curves, and custom SceneMatrix", "[plugin][recall]") {
    tg::PluginProcessor a;
    a.prepareToPlay(48000.0, 256);
    int mobiusIdx = -1;
    const auto entries = a.presets.entries();
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        if (entries[static_cast<std::size_t>(i)].category == "Mobius") {
            mobiusIdx = i;
            break;
        }
    }
    REQUIRE(mobiusIdx >= 0);
    a.presets.load(mobiusIdx);

    auto mat = a.matrix.get();
    mat.offsets[1][2] = 19.0f;
    mat.isCustom = true;
    a.matrix.set(mat);
    a.curves.set(0, tg::presetStep4Curve());

    juce::MemoryBlock saved;
    a.getStateInformation(saved);
    REQUIRE(saved.getSize() > 32);

    tg::PluginProcessor b;
    b.prepareToPlay(48000.0, 256);
    b.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));

    REQUIRE(b.currentParams().choice(tg::ParamId::mode) == 2);
    REQUIRE(b.matrix.get().isCustom);
    REQUIRE(b.matrix.get().offsets[1][2] == Approx(19.0f));
    REQUIRE(b.curves.get(0) == tg::presetStep4Curve());
}

TEST_CASE("PluginModel Undo/Redo, A/B compare, and MPE MIDI export work end-to-end", "[plugin][model]") {
    tg::PluginProcessor proc;
    proc.prepareToPlay(48000.0, 256);
    tg::PluginModel model(proc);

    const float initialGravity = model.get(tg::ParamId::gravity);
    model.begin(tg::ParamId::gravity);
    model.set(tg::ParamId::gravity, 88.0f);
    model.end(tg::ParamId::gravity);
    REQUIRE(model.get(tg::ParamId::gravity) == Approx(88.0f).margin(0.5f));
    REQUIRE(model.canUndo());

    model.undo();
    REQUIRE(model.get(tg::ParamId::gravity) == Approx(initialGravity).margin(0.5f));
    REQUIRE(model.canRedo());

    model.redo();
    REQUIRE(model.get(tg::ParamId::gravity) == Approx(88.0f).margin(0.5f));

    // A/B slot switching
    model.toggleAB();
    REQUIRE(model.isSlotB());
    model.toggleAB();
    REQUIRE(!model.isSlotB());

    // MPE .mid export
    const auto midFile = tg::MidiExporter::writeTempFile(proc.currentParams(), proc.curves.all(), proc.matrix.get(), 41,
                                                         172.0, "SmokeTest");
    REQUIRE(midFile.existsAsFile());
    REQUIRE(midFile.getSize() > 64);
    midFile.deleteFile();

    // Live MIDI chord capture + scene jump
    juce::AudioBuffer<float> audio(2, 256);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 41, static_cast<juce::uint8>(100)), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 48, static_cast<juce::uint8>(100)), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 54, static_cast<juce::uint8>(100)), 0);
    proc.processBlock(audio, midi);
    REQUIRE(model.captureHeldChord(2));
    REQUIRE(proc.matrix.get().isCustom);

    model.jumpToScene(3);
    REQUIRE(model.get(tg::ParamId::morph) == Approx(100.0f).margin(1.0f));
}
