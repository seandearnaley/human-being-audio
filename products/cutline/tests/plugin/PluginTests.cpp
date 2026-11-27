// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginModel.h"
#include "PluginProcessor.h"

#include "cl/groove_json.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <set>
#include <string>

using namespace cl;
using Catch::Approx;

namespace {
struct ScopedJuceGui {
    ScopedJuceGui() { juce::initialiseJuce_GUI(); }
    ~ScopedJuceGui() { juce::shutdownJuce_GUI(); }
};

// Moves a processor away from its defaults, so a reset is visible.
void disturb(PluginModel& model) {
    model.set(ParamId::density, 0.9f);
    model.set(ParamId::swing, 0.6f);
    GrooveBank bank = model.groove().bank;
    bank.scenes[0] = Pattern{};
    bank.scenes[2].lanes[3].steps[5].velocity = 96;
    model.setGrooveBank(bank);
}

void requireDefaultState(PluginProcessor& proc) {
    PluginModel model(proc);
    for (int i = 0; i < kParamCount; ++i) {
        const auto id = static_cast<ParamId>(i);
        INFO(std::string(spec(id).key));
        CHECK(model.get(id) == Approx(spec(id).def).margin(1e-3f));
    }
    CHECK(model.groove().bank == GrooveBank{});
    CHECK(proc.curves.all() == defaultLaneCurves());
}
} // namespace

TEST_CASE("The factory presets load: patterns and slicer presets in four categories", "[plugin]") {
    ScopedJuceGui gui;
    PluginProcessor proc;
    const auto& all = proc.presets.all();
    std::set<std::string> categories;
    int factory = 0;
    for (std::size_t i = 0; i < all.size(); ++i) {
        if (all[i].category == "User")
            continue; // presets saved on this machine
        ++factory;
        categories.insert(all[i].category);
        proc.presets.load(static_cast<int>(i));
        CHECK(proc.presets.currentName() == all[i].name);
        CHECK(proc.patterns.bank() == GrooveState(all[i].bank).bank);
    }
    CHECK(factory == 16);
    CHECK(categories == std::set<std::string>{"Breaks", "Neuro", "Club", "MIDI Slicer"});
}

TEST_CASE("A new instance opens on the default beat", "[plugin]") {
    ScopedJuceGui gui;
    PluginProcessor proc;
    requireDefaultState(proc);
    CHECK(proc.presets.currentName() == "Default Beat");
}

TEST_CASE("State round-trips parameters, curves and the groove bank", "[plugin]") {
    ScopedJuceGui gui;
    PluginProcessor a;
    PluginModel modelA(a);
    modelA.set(ParamId::density, 0.72f);
    modelA.set(ParamId::fillEvery, 2.0f);
    modelA.setLaneCurve(1, curves::sine());

    GrooveBank bank = modelA.groove().bank;
    setPatternBars(bank.scenes[0], 2);
    bank.scenes[0].lanes[6].length = 7;
    bank.scenes[0].lanes[4].steps[15].velocity = 71;
    bank.scenes[0].lanes[4].steps[15].ratchet = 3;
    bank.scenes[0].fill.steps[1][14].velocity = 118;
    bank.slicerLength = 12;
    modelA.setGrooveBank(bank);
    const GrooveBank stored = modelA.groove().bank; // sanitised

    juce::MemoryBlock blob;
    a.getStateInformation(blob);
    REQUIRE(blob.getSize() > 0);

    PluginProcessor b;
    b.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    PluginModel modelB(b);
    CHECK(modelB.get(ParamId::density) == Approx(0.72f).margin(1e-3f));
    CHECK(static_cast<int>(modelB.get(ParamId::fillEvery) + 0.5f) == 2);
    CHECK(modelB.laneCurve(1) == curves::sine());
    CHECK(modelB.groove().bank == stored);
}

TEST_CASE("Saving state does not republish the bank", "[plugin]") {
    ScopedJuceGui gui;
    PluginProcessor proc;
    const std::uint32_t revision = proc.patterns.revision();
    juce::MemoryBlock first;
    proc.getStateInformation(first);
    juce::MemoryBlock second;
    proc.getStateInformation(second);
    CHECK(proc.patterns.revision() == revision);
    CHECK(first == second);
}

TEST_CASE("A Cutline 2 session loads the default state", "[plugin]") {
    ScopedJuceGui gui;
    PluginProcessor proc;
    {
        PluginModel model(proc);
        disturb(model);
    }

    // What Cutline 2 wrote: no format attribute, a 2.0 pattern bank, parameters that no longer exist.
    juce::XmlElement old("Cutline");
    old.setAttribute("patternBank", R"({"scenes":[{"custom":true}]})");
    old.setAttribute("presetName", "Amen Chop 172");
    auto* param = old.createNewChildElement("PARAM");
    param->setAttribute("id", "gravity");
    param->setAttribute("value", 0.72);
    juce::MemoryBlock blob;
    juce::AudioProcessor::copyXmlToBinary(old, blob);

    proc.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    requireDefaultState(proc);
    CHECK(proc.presets.currentName() == "Default Beat");
}

TEST_CASE("State that does not parse loads the default state", "[plugin]") {
    ScopedJuceGui gui;

    PluginProcessor garbage;
    {
        PluginModel model(garbage);
        disturb(model);
    }
    const char junk[] = "this is not plugin state";
    garbage.setStateInformation(junk, static_cast<int>(sizeof(junk)));
    requireDefaultState(garbage);

    // The right format number, but a bank that is not a bank.
    PluginProcessor broken;
    {
        PluginModel model(broken);
        disturb(model);
    }
    juce::XmlElement xml("Cutline");
    xml.setAttribute("format", 3);
    xml.setAttribute("grooveBank", R"({"scenes":[{"bars":3}]})");
    juce::MemoryBlock blob;
    juce::AudioProcessor::copyXmlToBinary(xml, blob);
    broken.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    requireDefaultState(broken);
}

TEST_CASE("Undo, redo, clone and clear work on the groove bank", "[plugin]") {
    ScopedJuceGui gui;
    PluginProcessor proc;
    PluginModel model(proc);

    model.begin(ParamId::density);
    model.set(ParamId::density, 0.85f);
    model.end(ParamId::density);
    REQUIRE(model.canUndo());
    model.undo();
    CHECK(model.get(ParamId::density) == Approx(spec(ParamId::density).def).margin(1e-3f));
    REQUIRE(model.canRedo());
    model.redo();
    CHECK(model.get(ParamId::density) == Approx(0.85f).margin(1e-3f));

    const Pattern sceneA = model.groove().bank.scenes[0];
    model.cloneActiveSceneToNext();
    CHECK(static_cast<int>(model.get(ParamId::scene) + 0.5f) == 1);
    CHECK(model.groove().bank.scenes[1] == sceneA); // an exact copy: nothing is mutated

    model.clearActiveScene();
    CHECK(model.groove().bank.scenes[1] == Pattern{});
    CHECK(model.groove().bank.scenes[0] == sceneA);
    model.undo();
    CHECK(model.groove().bank.scenes[1] == sceneA);

    CHECK(model.exportStatus() == "Drag 1-bar .mid");
}

TEST_CASE("The model serves a sanitised bank with the engine's density maps", "[plugin]") {
    ScopedJuceGui gui;
    PluginProcessor proc;
    PluginModel model(proc);
    GrooveBank bank = model.groove().bank;
    bank.scenes[0].lanes[0].steps[40].velocity = 96; // beyond a one-bar pattern
    bank.scenes[0].lanes[4].steps[3].velocity = 56;
    bank.scenes[0].lanes[4].steps[3].layer = Layer::detail;
    model.setGrooveBank(bank);

    const GrooveState& groove = model.groove();
    CHECK_FALSE(groove.bank.scenes[0].lanes[0].steps[40].on());
    CHECK(groove.maps[0].onAt[0][0] == Approx(0.0f)); // Core
    CHECK(groove.maps[0].onAt[4][3] == Approx(1.0f)); // the only Detail step arrives at 100%
    CHECK(groove.bank == proc.patterns.bank());
}

TEST_CASE("processBlock renders finite audition audio", "[plugin]") {
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
