// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginModel.h"
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

    // Exported SMF parses cleanly through JUCE MidiFile (with overrides & held notes)
    cl::Params defaults;
    cl::HeldNotes held;
    const auto midFile =
        proc.exporter.writeTempFile(defaults, proc.curves.all(), proc.overrides.get(), held, 172.0, 0, "TestExport");
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

    // Round-trip state including custom curve and phrase overrides
    proc.curves.set(0, cl::curves::rampDown());
    cl::PhraseOverrides ov;
    ov.spans[0].locked = true;
    ov.spans[0].lockedSeed = 407;
    ov.fillStartFrac = 0.72f;
    proc.overrides.set(ov);
    if (auto* param = proc.state.getParameter("gravity"))
        param->setValueNotifyingHost(0.75f);

    juce::MemoryBlock stateBlob;
    proc.getStateInformation(stateBlob);

    cl::PluginProcessor restored;
    restored.prepareToPlay(48000.0, 257);
    restored.setStateInformation(stateBlob.getData(), static_cast<int>(stateBlob.getSize()));

    REQUIRE(restored.curves.get(0) == cl::curves::rampDown());
    REQUIRE(restored.overrides.get() == ov);
    REQUIRE(restored.state.getParameter("gravity")->getValue() == Catch::Approx(0.75f).margin(1e-4f));
}

TEST_CASE(
    "Cutline PluginModel supports Undo/Redo, drag coalescing, and A/B compare across params, curves, and overrides",
    "[plugin]") {
    juce::ScopedJuceInitialiser_GUI juceInit;

    cl::PluginProcessor proc;
    proc.prepareToPlay(48000.0, 256);
    cl::PluginModel model(proc);

    CHECK_FALSE(model.canUndo());
    CHECK_FALSE(model.canRedo());
    CHECK_FALSE(model.isSlotB());

    const float initialGravity = model.get(cl::ParamId::gravity);
    model.begin(cl::ParamId::gravity);
    model.set(cl::ParamId::gravity, 0.85f);
    model.end(cl::ParamId::gravity);
    CHECK(model.canUndo());
    CHECK(model.get(cl::ParamId::gravity) == Catch::Approx(0.85f).margin(1e-3f));

    // Rapid consecutive override edits (simulating a continuous note/divider drag) coalesce into one undo step.
    cl::PhraseOverrides ov1;
    ov1.fillStartFrac = 0.68f;
    model.setOverrides(ov1);
    cl::PhraseOverrides ov2 = ov1;
    ov2.fillStartFrac = 0.74f;
    model.setOverrides(ov2);
    CHECK(model.overrides() == ov2);

    // One undo reverts the entire coalesced override drag back to empty overrides.
    model.undo();
    CHECK_FALSE(model.overrides().hasAny());
    CHECK(model.canRedo());

    // Second undo reverts the gravity parameter edit.
    model.undo();
    CHECK(model.get(cl::ParamId::gravity) == Catch::Approx(initialGravity).margin(1e-3f));

    // Redo restores both steps cleanly.
    model.redo();
    CHECK(model.get(cl::ParamId::gravity) == Catch::Approx(0.85f).margin(1e-3f));
    model.redo();
    CHECK(model.overrides() == ov2);

    // A/B compare isolates edits and supports copy active -> other.
    model.toggleAB();
    CHECK(model.isSlotB());
    CHECK(model.get(cl::ParamId::gravity) == Catch::Approx(0.85f).margin(1e-3f));

    model.begin(cl::ParamId::gravity);
    model.set(cl::ParamId::gravity, 0.20f);
    model.end(cl::ParamId::gravity);
    model.setLaneCurve(0, cl::curves::rampUp());

    model.toggleAB();
    CHECK_FALSE(model.isSlotB());
    CHECK(model.get(cl::ParamId::gravity) == Catch::Approx(0.85f).margin(1e-3f));
    CHECK_FALSE(model.laneCurve(0) == cl::curves::rampUp());

    model.toggleAB();
    CHECK(model.isSlotB());
    CHECK(model.get(cl::ParamId::gravity) == Catch::Approx(0.20f).margin(1e-3f));
    CHECK(model.laneCurve(0) == cl::curves::rampUp());

    model.copyAB();
    model.toggleAB();
    CHECK_FALSE(model.isSlotB());
    CHECK(model.get(cl::ParamId::gravity) == Catch::Approx(0.20f).margin(1e-3f));
    CHECK(model.laneCurve(0) == cl::curves::rampUp());
}
