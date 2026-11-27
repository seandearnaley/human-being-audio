// SPDX-License-Identifier: AGPL-3.0-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "MidiExporter.h"
#include "PluginModel.h"
#include "PluginProcessor.h"
#include "tg/export.h"

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

TEST_CASE("The dragged MIDI file keeps five unison voices on separate named tracks", "[plugin][mpe-export]") {
    tg::RenderRequest phrase;
    const auto bytes = tg::exportMpeSmf(phrase.params, phrase.curves, phrase.matrix, 41, 120.0);
    juce::MemoryInputStream input(bytes.data(), bytes.size(), false);
    juce::MidiFile file;
    int format = -1;
    REQUIRE(file.readFrom(input, false, &format));
    REQUIRE(format == 1);
    REQUIRE(file.getNumTracks() == 6);
    CHECK(file.getTimeFormat() == 960);

    for (int index = 0; index < file.getNumTracks(); ++index) {
        CAPTURE(index);
        const auto& track = *file.getTrack(index);
        juce::String name;
        int tempos = 0, notes = 0, releases = 0, bends = 0;
        std::vector<std::pair<int, int>> controllers;
        for (const auto* event : track) {
            const auto& message = event->message;
            if (message.isTrackNameEvent())
                name = message.getTextFromTextMetaEvent();
            if (message.isTempoMetaEvent()) {
                ++tempos;
                CHECK(message.getTempoSecondsPerQuarterNote() == Approx(0.5));
            }
            if (message.isMetaEvent())
                continue;
            REQUIRE(message.getChannel() == index + 1);
            if (message.isController()) {
                CHECK(message.getTimeStamp() == 0.0);
                controllers.emplace_back(message.getControllerNumber(), message.getControllerValue());
            } else if (message.isNoteOn() || message.isNoteOff()) {
                CHECK(message.getNoteNumber() == 41);
                if (message.isNoteOn()) {
                    ++notes;
                    CHECK(message.getTimeStamp() == 0.0);
                } else {
                    ++releases;
                    CHECK(message.getTimeStamp() == 15356.0);
                }
            } else if (message.isPitchWheel()) {
                CHECK(message.getTimeStamp() == bends * 60.0);
                CHECK(message.getPitchWheelValue() == 8192 + 8 * bends);
                ++bends;
            }
        }
        REQUIRE(track.getEndTime() == 15360.0);
        if (index == 0) {
            CHECK(name == "MPE setup");
            CHECK(tempos == 1);
            CHECK(controllers == (std::vector<std::pair<int, int>>{{101, 0}, {100, 6}, {6, 5}}));
            CHECK(notes + releases + bends == 0);
        } else {
            CHECK(name == "Voice " + juce::String(index));
            CHECK(tempos == 0);
            CHECK(controllers == (std::vector<std::pair<int, int>>{{101, 0}, {100, 0}, {6, 48}, {38, 0}}));
            CHECK(notes == 1);
            CHECK(releases == 1);
            CHECK(bends == 256);
        }
    }
}

TEST_CASE("torus-glide renders a mono host block beyond the prepared size", "[plugin][oversized]") {
    juce::ScopedJuceInitialiser_GUI gui;
    tg::PluginProcessor proc;
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

TEST_CASE("torus-glide renders its longest release inside the reported host tail", "[plugin][tail]") {
    juce::ScopedJuceInitialiser_GUI gui;
    tg::PluginProcessor proc;
    constexpr int rate = 22050, block = 256;
    proc.setRateAndBufferSizeDetails(rate, block);
    proc.prepareToPlay(rate, block);
    proc.state.getParameter("releaseMs")->setValueNotifyingHost(1.0f);
    juce::AudioBuffer<float> audio(2, block);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 36, 0.95f), 0);
    for (int i = 0; i < 40; ++i) {
        audio.clear();
        proc.processBlock(audio, midi);
        midi.clear();
    }
    midi.addEvent(juce::MidiMessage::noteOff(1, 36), 0);
    const int end = static_cast<int>(std::ceil(proc.getTailLengthSeconds() * rate));
    REQUIRE(end < rate * 60);
    float peakAfter = 0.0f, peakBefore = 0.0f;
    for (int start = 0; start < end + 2 * block; start += block) {
        audio.clear();
        proc.processBlock(audio, midi);
        midi.clear();
        if (start == 0)
            peakBefore = audio.getMagnitude(0, block);
        const int from = std::clamp(end - start, 0, block);
        if (from < block)
            peakAfter = std::max(peakAfter, audio.getMagnitude(from, block - from));
    }
    REQUIRE(peakBefore > 0.01f);
    CHECK(peakAfter < 1.0e-6f);
}
