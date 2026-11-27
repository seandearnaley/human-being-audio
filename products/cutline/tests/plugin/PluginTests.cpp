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
#include <utility>

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

TEST_CASE("State with another root tag, or with no bank, loads the default state", "[plugin]") {
    ScopedJuceGui gui;
    const auto load = [](const juce::XmlElement& xml) {
        PluginProcessor proc;
        {
            PluginModel model(proc);
            disturb(model);
        }
        juce::MemoryBlock blob;
        juce::AudioProcessor::copyXmlToBinary(xml, blob);
        proc.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
        requireDefaultState(proc);
    };

    // The right format and a valid bank, under a root tag that is not Cutline's.
    juce::XmlElement other("NotCutline");
    other.setAttribute("format", 3);
    other.setAttribute("grooveBank", toJuce(serialiseGrooveBank(GrooveBank{})));
    load(other);

    // Cutline's tag and format 3, a parameter away from its default, and no bank.
    juce::XmlElement noBank("Cutline");
    noBank.setAttribute("format", 3);
    auto* param = noBank.createNewChildElement("PARAM");
    param->setAttribute("id", "density");
    param->setAttribute("value", 0.9);
    load(noBank);
}

namespace {

// A host transport that is always playing at 120 BPM. The test advances it by each block's length.
struct FakePlayHead final : juce::AudioPlayHead {
    double ppq = 0.0;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo info;
        info.setBpm(120.0);
        info.setPpqPosition(ppq);
        info.setIsPlaying(true);
        return info;
    }
};

// Plays one bar (4 beats at 120 BPM and 48 kHz: 375 blocks of 256 samples) and returns the note numbers of its
// note-ons.
std::multiset<int> playBar(PluginProcessor& proc, FakePlayHead& head) {
    constexpr int kBlock = 256;
    juce::AudioBuffer<float> buf(2, kBlock);
    juce::MidiBuffer midi;
    std::multiset<int> notes;
    for (int i = 0; i < 375; ++i) {
        buf.clear();
        midi.clear();
        proc.processBlock(buf, midi);
        for (const auto metadata : midi) {
            const auto msg = metadata.getMessage();
            if (msg.isNoteOn())
                notes.insert(msg.getNoteNumber());
        }
        head.ppq += 120.0 / 60.0 / 48000.0 * kBlock;
    }
    return notes;
}

int laneNote(int lane) {
    return kKitNoteMaps[static_cast<std::size_t>(spec(ParamId::noteMap).def)][static_cast<std::size_t>(lane)];
}

} // namespace

TEST_CASE("With the transport playing, the plugin plays the bank it holds", "[plugin]") {
    ScopedJuceGui gui;
    PluginProcessor proc;
    FakePlayHead head;
    proc.setPlayHead(&head);
    proc.prepareToPlay(48000.0, 256);

    // The default beat: two kicks, two snares and eight closed hats.
    const std::multiset<int> first = playBar(proc, head);
    CHECK(first.size() == 12);
    CHECK(first.count(laneNote(0)) == 2);
    CHECK(first.count(laneNote(1)) == 2);
    CHECK(first.count(laneNote(4)) == 8);

    // An edit through the model: the hat lane cleared. The next bar plays without it.
    {
        PluginModel model(proc);
        GrooveBank bank = model.groove().bank;
        for (auto& step : bank.scenes[0].lanes[4].steps)
            step = {};
        model.setGrooveBank(bank);
    }
    const std::multiset<int> edited = playBar(proc, head);
    CHECK(edited.size() == 4);
    CHECK(edited.count(laneNote(4)) == 0);

    // A saved session with another bank: four on the floor on the kick only.
    PluginProcessor other;
    {
        PluginModel model(other);
        GrooveBank bank;
        bank.scenes[0] = Pattern{};
        for (const int step : {0, 4, 8, 12}) {
            bank.scenes[0].lanes[0].steps[static_cast<std::size_t>(step)].velocity = 100;
            bank.scenes[0].lanes[0].steps[static_cast<std::size_t>(step)].layer = Layer::core;
        }
        model.setGrooveBank(bank);
    }
    juce::MemoryBlock blob;
    other.getStateInformation(blob);
    proc.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    const std::multiset<int> loaded = playBar(proc, head);
    CHECK(loaded == std::multiset<int>{laneNote(0), laneNote(0), laneNote(0), laneNote(0)});
    proc.setPlayHead(nullptr);
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

namespace {

// Counts writes of the groove bank into the plugin's state tree.
struct BankWrites final : juce::ValueTree::Listener {
    explicit BankWrites(juce::ValueTree tree) : tree_(std::move(tree)) { tree_.addListener(this); }
    ~BankWrites() override { tree_.removeListener(this); }
    void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier& property) override {
        if (property == kGrooveBankProp)
            ++count;
    }
    int count = 0;

  private:
    juce::ValueTree tree_;
};

GrooveBank withHat(GrooveBank bank, int step) {
    bank.scenes[0].lanes[4].steps[static_cast<std::size_t>(step)].velocity = 64;
    bank.scenes[0].lanes[4].steps[static_cast<std::size_t>(step)].layer = Layer::detail;
    return bank;
}

} // namespace

TEST_CASE("One grid gesture is one undo step and one state write", "[plugin]") {
    ScopedJuceGui gui;
    PluginProcessor proc;
    PluginModel model(proc);
    BankWrites writes(proc.state.state);
    const GrooveBank original = model.groove().bank;
    const auto storedBank = [&proc] { return proc.state.state.getProperty(kGrooveBankProp).toString(); };
    const juce::String storedBefore = storedBank();

    // A paint drag across eight cells: every change reaches the engine at once, the state tree only at the end.
    GrooveBank bank = original;
    model.beginGrooveEdit();
    for (int step = 1; step < 16; step += 2) {
        bank = withHat(bank, step);
        model.setGrooveBank(bank);
        CHECK(proc.patterns.bank() == GrooveState(bank).bank);
    }
    CHECK(writes.count == 0);
    CHECK(storedBank() == storedBefore);
    model.endGrooveEdit();
    CHECK(writes.count == 1);
    CHECK(storedBank() == toJuce(serialiseGrooveBank(GrooveState(bank).bank)));

    REQUIRE(model.canUndo());
    model.undo();
    CHECK(model.groove().bank == original);
    CHECK_FALSE(model.canUndo());
    model.redo();
    CHECK(model.groove().bank == GrooveState(bank).bank);
    model.undo();

    // Two clicks 100 ms apart are two gestures, so two undo steps.
    const GrooveBank first = withHat(original, 1);
    const GrooveBank second = withHat(first, 3);
    model.beginGrooveEdit();
    model.setGrooveBank(first);
    model.endGrooveEdit();
    juce::Thread::sleep(100);
    model.beginGrooveEdit();
    model.setGrooveBank(second);
    model.endGrooveEdit();
    model.undo();
    CHECK(model.groove().bank == GrooveState(first).bank);
    model.undo();
    CHECK(model.groove().bank == original);

    // A gesture that edits nothing takes no snapshot and writes nothing.
    const int before = writes.count;
    model.beginGrooveEdit();
    model.endGrooveEdit();
    CHECK(writes.count == before);
    CHECK_FALSE(model.canUndo());
}

TEST_CASE("Edits outside a gesture are each their own undo step, written at once", "[plugin]") {
    ScopedJuceGui gui;
    PluginProcessor proc;
    PluginModel model(proc);
    BankWrites writes(proc.state.state);
    const GrooveBank original = model.groove().bank;
    const GrooveBank first = withHat(original, 1);
    const GrooveBank second = withHat(first, 3);
    model.setGrooveBank(first);  // a menu choice
    model.setGrooveBank(second); // Vary, straight after: no coalescing
    CHECK(writes.count == 2);
    model.undo();
    CHECK(model.groove().bank == GrooveState(first).bank);
    model.undo();
    CHECK(model.groove().bank == original);
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
