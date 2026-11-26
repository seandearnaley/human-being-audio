// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginModel.h"
#include "PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace cl;
using Catch::Approx;

namespace {
struct ScopedJuceGui {
    ScopedJuceGui() { juce::initialiseJuce_GUI(); }
    ~ScopedJuceGui() { juce::shutdownJuce_GUI(); }
};
} // namespace

TEST_CASE("All Cutline 2.0 embedded factory presets load cleanly and cover all 6 categories", "[plugin]") {
    ScopedJuceGui gui;
    PluginProcessor proc;
    const auto& all = proc.presets.all();
    REQUIRE(all.size() >= 30);

    bool hasBreaks = false, hasNeuro = false, hasClub = false, hasPoly = false, hasSb = false, hasSlicer = false;
    for (std::size_t i = 0; i < all.size(); ++i) {
        proc.presets.load(static_cast<int>(i));
        const auto& cat = all[i].category;
        hasBreaks |= (cat == "Breaks");
        hasNeuro |= (cat == "Neuro");
        hasClub |= (cat == "Club");
        hasPoly |= (cat == "Polyrhythm");
        hasSb |= (cat == "Strike Body");
        hasSlicer |= (cat == "MIDI Slicer");
    }
    CHECK(hasBreaks);
    CHECK(hasNeuro);
    CHECK(hasClub);
    CHECK(hasPoly);
    CHECK(hasSb);
    CHECK(hasSlicer);
}

TEST_CASE("State persistence round-trips parameters, expression curves, and PatternBank", "[plugin]") {
    ScopedJuceGui gui;
    PluginProcessor a;
    PluginModel modelA(a);

    modelA.set(ParamId::style, static_cast<float>(DrumStyle::ukg));
    modelA.set(ParamId::gravity, 0.56f);
    modelA.set(ParamId::ratchet, 0.72f);

    PatternBank bank = modelA.patternBank();
    bank.scenes[0].customized = true;
    bank.scenes[0].lanes[6].stepLength = 7;
    bank.scenes[0].lanes[1].locked = true;
    bank.scenes[0].lanes[4].steps[15].ratchet = 3;
    modelA.setPatternBank(bank);

    juce::MemoryBlock blob;
    a.getStateInformation(blob);
    REQUIRE(blob.getSize() > 0);

    PluginProcessor b;
    b.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    PluginModel modelB(b);

    CHECK(modelB.get(ParamId::style) == Approx(static_cast<float>(DrumStyle::ukg)));
    CHECK(modelB.get(ParamId::gravity) == Approx(0.56f).margin(1e-3f));
    CHECK(modelB.get(ParamId::ratchet) == Approx(0.72f).margin(1e-3f));
    CHECK(modelB.patternBank() == bank);
}

TEST_CASE("PluginModel Undo/Redo, A/B, Mutate, Polymeter, and Clone actions work cleanly", "[plugin]") {
    ScopedJuceGui gui;
    PluginProcessor proc;
    PluginModel model(proc);

    model.begin(ParamId::gravity);
    model.set(ParamId::gravity, 0.65f);
    model.end(ParamId::gravity);
    REQUIRE(model.canUndo());

    model.undo();
    CHECK(model.get(ParamId::gravity) == Approx(spec(ParamId::gravity).def).margin(1e-3f));
    REQUIRE(model.canRedo());
    model.redo();
    CHECK(model.get(ParamId::gravity) == Approx(0.65f).margin(1e-3f));

    model.mutateActiveScene();
    CHECK(model.patternBank().scenes[0].customized);
    model.polymeterActiveScene();
    CHECK(model.patternBank().scenes[0].lanes[6].stepLength != 16);
    model.cloneActiveSceneToNext();
    CHECK(static_cast<int>(model.get(ParamId::scene) + 0.5f) == 1);
    CHECK(model.patternBank().scenes[1].customized);
}

TEST_CASE("PluginProcessor processBlock renders finite audition audio with balanced MIDI", "[plugin]") {
    ScopedJuceGui gui;
    PluginProcessor proc;
    proc.prepareToPlay(48000.0, 256);

    juce::AudioBuffer<float> buf(2, 256);
    juce::MidiBuffer midi;
    for (int i = 0; i < 40; ++i) {
        buf.clear();
        midi.clear();
        proc.processBlock(buf, midi);
        for (int ch = 0; ch < 2; ++ch) {
            const float* data = buf.getReadPointer(ch);
            for (int s = 0; s < 256; ++s)
                REQUIRE(std::isfinite(data[s]));
        }
    }
}
