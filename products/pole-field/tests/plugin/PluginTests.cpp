#include "PluginModel.h"
#include "PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

TEST_CASE("PoleField processor processes audio, loads all factory presets, and round-trips state", "[plugin]") {
    juce::ScopedJuceInitialiser_GUI juceInit;

    pf::PluginProcessor proc;
    proc.prepareToPlay(48000.0, 256);

    REQUIRE(proc.presets.all().size() >= 10);

    juce::AudioBuffer<float> audio(2, 256);
    juce::MidiBuffer midi;

    for (std::size_t idx = 0; idx < proc.presets.all().size(); ++idx) {
        proc.presets.load(static_cast<int>(idx));
        for (int i = 0; i < 256; ++i) {
            const float v = 0.35f * std::sin(2.0f * 3.14159265f * 110.0f * static_cast<float>(i) / 48000.0f)
                          + 0.25f * std::sin(2.0f * 3.14159265f * 880.0f * static_cast<float>(i) / 48000.0f);
            audio.setSample(0, i, v);
            audio.setSample(1, i, v);
        }
        proc.processBlock(audio, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 256; ++i)
                REQUIRE(std::isfinite(audio.getSample(c, i)));
    }

    // Round-trip state including imprint + custom curve
    pf::ImprintData imp;
    imp.active = true;
    imp.corner = pf::cubeAt(1).corners[2];
    proc.imprint.set(imp, "TestSample");
    proc.curves.set(0, pf::curves::rampUp());

    juce::MemoryBlock stateBlob;
    proc.getStateInformation(stateBlob);

    pf::PluginProcessor restored;
    restored.prepareToPlay(48000.0, 256);
    restored.setStateInformation(stateBlob.getData(), static_cast<int>(stateBlob.getSize()));

    REQUIRE(restored.imprint.get().active);
    REQUIRE(restored.imprint.label() == "TestSample");
    REQUIRE(restored.curves.get(0) == pf::curves::rampUp());
}

TEST_CASE("PoleField processor responds to MIDI CC1, CC74, Channel Pressure, CC121, and RPN 00 00", "[plugin]") {
    juce::ScopedJuceInitialiser_GUI juceInit;

    pf::PluginProcessor proc;
    proc.prepareToPlay(48000.0, 256);

    if (auto* motion = proc.state.getParameter("motion"))
        motion->setValueNotifyingHost(0.0f);
    if (auto* pt = proc.state.getParameter("pitchTrack"))
        pt->setValueNotifyingHost(1.0f);
    if (auto* glide = proc.state.getParameter("glide"))
        glide->setValueNotifyingHost(0.0f);

    juce::AudioBuffer<float> audio(2, 256);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 101, 0), 0);
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 100, 0), 0);
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 6, 12), 0); // RPN 00 00 = 12 st
    midi.addEvent(juce::MidiMessage::noteOn(1, 29, 1.0f), 0);       // F1 = 0 st
    midi.addEvent(juce::MidiMessage::pitchWheel(1, 16383), 0);      // +12 st
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 74, 127), 0);
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 1, 127), 0);
    midi.addEvent(juce::MidiMessage::channelPressureChange(1, 127), 0);

    audio.clear();
    proc.processBlock(audio, midi);
    juce::MidiBuffer empty;
    for (int i = 0; i < 16; ++i) {
        audio.clear();
        proc.processBlock(audio, empty);
    }

    pf::ui::Visuals visuals;
    proc.analysis.update(visuals, 0.016f);
    CHECK(visuals.midiShiftSt == Catch::Approx(12.0f).margin(0.2f));
    CHECK(visuals.effMorphX > 0.70f);
    CHECK(visuals.effMorphY > 0.65f);
    CHECK(visuals.effMorphZ > 0.50f);
    CHECK(visuals.effDrive > 0.50f);

    // CC121 resets expression and pitch bend cleanly.
    juce::MidiBuffer resetMidi;
    resetMidi.addEvent(juce::MidiMessage::controllerEvent(1, 121, 0), 0);
    audio.clear();
    proc.processBlock(audio, resetMidi);
    for (int i = 0; i < 16; ++i) {
        audio.clear();
        proc.processBlock(audio, empty);
    }
    proc.analysis.update(visuals, 0.016f);
    CHECK(visuals.midiShiftSt == Catch::Approx(0.0f).margin(0.2f));
}

TEST_CASE("PoleField PluginModel supports Undo/Redo and A/B compare across params, curves, and imprint", "[plugin]") {
    juce::ScopedJuceInitialiser_GUI juceInit;

    pf::PluginProcessor proc;
    proc.prepareToPlay(48000.0, 256);
    pf::PluginModel model(proc);

    CHECK_FALSE(model.canUndo());
    CHECK_FALSE(model.isSlotB());

    const float initialX = model.get(pf::ParamId::morphX);
    model.begin(pf::ParamId::morphX);
    model.set(pf::ParamId::morphX, 0.88f);
    model.end(pf::ParamId::morphX);
    CHECK(model.canUndo());

    model.setLaneCurve(1, pf::curves::rampDown());
    CHECK(model.laneCurve(1) == pf::curves::rampDown());

    model.undo();
    CHECK_FALSE(model.laneCurve(1) == pf::curves::rampDown());
    model.undo();
    CHECK(model.get(pf::ParamId::morphX) == Catch::Approx(initialX).margin(1e-3f));

    model.redo();
    CHECK(model.get(pf::ParamId::morphX) == Catch::Approx(0.88f).margin(1e-3f));

    // A/B compare swaps parameter + imprint state cleanly.
    pf::ImprintData imp;
    imp.active = true;
    imp.corner = pf::cubeAt(1).corners[0];
    proc.imprint.set(imp, "SlotA");

    model.toggleAB();
    CHECK(model.isSlotB());
    CHECK(model.imprint().active);
    model.clearImprint();
    CHECK_FALSE(model.imprint().active);
    model.begin(pf::ParamId::morphX);
    model.set(pf::ParamId::morphX, 0.15f);
    model.end(pf::ParamId::morphX);

    model.toggleAB();
    CHECK_FALSE(model.isSlotB());
    CHECK(model.get(pf::ParamId::morphX) == Catch::Approx(0.88f).margin(1e-3f));
    CHECK(model.imprint().active);
    CHECK(model.imprintLabel() == "SlotA");

    model.copyAB();
    model.toggleAB();
    CHECK(model.isSlotB());
    CHECK(model.get(pf::ParamId::morphX) == Catch::Approx(0.88f).margin(1e-3f));
    CHECK(model.imprint().active);

    // Processor owns history so Undo/Redo and A/B survive closing and reopening the editor.
    {
        pf::PluginModel reopened(proc);
        CHECK(reopened.isSlotB());
        CHECK(reopened.canUndo());
    }
}

TEST_CASE("PoleField preset load clears stale imprint and oversized mono blocks render cleanly", "[plugin]") {
    juce::ScopedJuceInitialiser_GUI juceInit;

    pf::PluginProcessor proc;
    proc.prepareToPlay(48000.0, 256);

    pf::ImprintData imp;
    imp.active = true;
    imp.corner = pf::cubeAt(1).corners[2];
    proc.imprint.set(imp, "StaleImprint");
    REQUIRE(proc.imprint.get().active);

    proc.presets.load(0);
    CHECK_FALSE(proc.imprint.get().active);

    // Oversized mono block (> 4096 samples) chunks safely instead of clearing to silence.
    juce::AudioBuffer<float> mono(1, 5000);
    for (int i = 0; i < 5000; ++i)
        mono.setSample(0, i, 0.35f * std::sin(2.0f * 3.14159265f * 220.0f * static_cast<float>(i) / 48000.0f));
    juce::MidiBuffer midi;
    proc.processBlock(mono, midi);
    float peak = 0.0f;
    for (int i = 0; i < 5000; ++i) {
        REQUIRE(std::isfinite(mono.getSample(0, i)));
        peak = std::max(peak, std::abs(mono.getSample(0, i)));
    }
    CHECK(peak > 0.05f);
}
