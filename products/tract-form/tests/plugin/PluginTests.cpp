#include "PluginEditor.h"
#include "PluginModel.h"
#include "PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <thread>
#include <vector>

#if JUCE_MAC
// libmalloc calls this hook, when set, for every allocation and free. It is how
// Apple's own memory tools watch a process, and it sees malloc as well as new.
extern "C" void (*malloc_logger)(uint32_t type, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uint32_t);
#endif

using Catch::Approx;
using namespace tf;

namespace {
constexpr double kSampleRate = 48000.0;
constexpr int kBlock = 512;

struct Host {
    juce::ScopedJuceInitialiser_GUI juce;
    PluginProcessor processor;
    juce::AudioBuffer<float> buffer { 2, kBlock };

    Host() {
        processor.setPlayConfigDetails(0, 2, kSampleRate, kBlock);
        processor.prepareToPlay(kSampleRate, kBlock);
    }

    // Runs `blocks` blocks; `first` is delivered at the start of the first one.
    float run(int blocks, const juce::MidiMessage* first = nullptr) {
        float peak = 0.0f;
        for (int i = 0; i < blocks; ++i) {
            juce::MidiBuffer midi;
            if (i == 0 && first != nullptr) midi.addEvent(*first, 0);
            buffer.clear();
            processor.processBlock(buffer, midi);
            peak = std::max(peak, buffer.getMagnitude(0, kBlock));
        }
        return peak;
    }
};

#if JUCE_MAC
// Counts this thread's allocations while one is alive.
class AllocationCount {
public:
    AllocationCount() {
        count_ = 0;
        malloc_logger = [](uint32_t type, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uint32_t) {
            constexpr uint32_t allocation = 2;
            if ((type & allocation) != 0) ++count_;
        };
    }
    ~AllocationCount() { malloc_logger = nullptr; }
    int get() const { return count_; }

private:
    static thread_local int count_;
};
thread_local int AllocationCount::count_ = 0;
#endif

// Lets the message thread's queued callbacks run.
void runMessageLoop() { juce::MessageManager::getInstance()->runDispatchLoopUntil(50); }

float bandHz(int band) {
    return 20.0f * std::pow(1000.0f, (static_cast<float>(band) + 0.5f) / ui::Visuals::kSpectrumBands);
}
}

TEST_CASE("a note sounds and silence returns after it") {
    Host host;
    CHECK(host.run(8) == 0.0f);

    const auto on = juce::MidiMessage::noteOn(1, 29, 1.0f);
    CHECK(host.run(60, &on) > 0.1f);

    const auto off = juce::MidiMessage::noteOff(1, 29);
    host.run(60, &off);
    CHECK(host.run(8) == 0.0f);
}

TEST_CASE("the interface sees what is playing") {
    Host host;
    PluginModel model(host.processor);

    const auto on = juce::MidiMessage::noteOn(1, 29, 1.0f);
    host.run(4, &on);
    model.visuals();  // drain the start-up
    host.run(40);
    const auto& visuals = model.visuals();

    CHECK(visuals.gate);
    CHECK(visuals.note == 29);
    CHECK(visuals.hz == Approx(43.65f).margin(0.5f));

    // The scope is locked to the master: it starts at a rising zero crossing.
    CHECK(std::abs(visuals.scopeMaster[0]) < 0.1f);
    CHECK(visuals.scopeMaster[ui::Visuals::kScopePoints / 8] > 0.5f);
    CHECK(*std::max_element(visuals.scopeOut.begin(), visuals.scopeOut.end()) > 0.05f);

    // The loudest thing below 100 Hz is the sub at the note's pitch.
    int loudest = 0;
    for (int band = 0; bandHz(band) < 100.0f; ++band)
        if (visuals.spectrumDb[static_cast<std::size_t>(band)] > visuals.spectrumDb[static_cast<std::size_t>(loudest)]) loudest = band;
    CHECK(bandHz(loudest) == Approx(43.65f).margin(8.0f));
    CHECK(visuals.spectrumDb[static_cast<std::size_t>(loudest)] > -30.0f);

    CHECK(visuals.peakDbLeft > -30.0f);
    CHECK(visuals.correlation > 0.0f);
}

TEST_CASE("the plugin reports the engine's latency") {
    Host host;
    CHECK(host.processor.getLatencySamples() > 0);
    CHECK(host.processor.getLatencySamples() < 256);
}

TEST_CASE("state survives a save and restore") {
    Host host;
    PluginModel model(host.processor);
    model.set(ParamId::coupling, 0.25f);
    model.set(ParamId::vowelB, 3.0f);
    model.set(ParamId::lane2Depth, -0.5f);

    juce::MemoryBlock saved;
    host.processor.getStateInformation(saved);

    Host restored;
    restored.processor.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    PluginModel restoredModel(restored.processor);
    CHECK(restoredModel.get(ParamId::coupling) == Approx(0.25f));
    CHECK(restoredModel.get(ParamId::vowelB) == Approx(3.0f));
    CHECK(restoredModel.get(ParamId::lane2Depth) == Approx(-0.5f));
    CHECK(restoredModel.presetName() == model.presetName());
}

TEST_CASE("every factory preset loads") {
    Host host;
    PluginModel model(host.processor);
    std::vector<std::string> names;
    for (const auto& preset : model.presets()) names.push_back(preset.name);
    REQUIRE(names.size() >= 28);

    for (int i = 0; i < static_cast<int>(names.size()); ++i) {
        model.loadPreset(i);
        CHECK(model.presetName() == names[static_cast<std::size_t>(i)]);
    }

    // Stepping wraps around both ends.
    model.loadPreset(0);
    model.stepPreset(-1);
    CHECK(model.presetName() == names.back());
    model.stepPreset(1);
    CHECK(model.presetName() == names.front());
}

TEST_CASE("the parameter the host sees maps exactly like the core table") {
    Host host;
    PluginModel model(host.processor);
    for (std::size_t i = 0; i < kParams.size(); ++i) {
        const auto id = static_cast<ParamId>(i);
        CAPTURE(kParams[i].key);
        CHECK(model.get(id) == Approx(kParams[i].def).margin(1e-3));
        model.set(id, kParams[i].max);
        CHECK(model.get(id) == Approx(kParams[i].max).margin(1e-3));
        model.set(id, kParams[i].min);
        CHECK(model.get(id) == Approx(kParams[i].min).margin(1e-3));
    }
}

TEST_CASE("a drawn curve is saved with the session and with a preset's lane") {
    Host host;
    PluginModel model(host.processor);
    const Curve drawn = curves::make({ { 0.0f, -1.0f, 0.4f, Segment::line }, { 0.5f, 1.0f, 0.0f, Segment::hold } });
    model.set(laneParam(2, LaneField::shape), static_cast<float>(LaneShape::custom));
    model.setLaneCurve(2, drawn);
    CHECK(model.laneCurve(2) == drawn);

    juce::MemoryBlock saved;
    host.processor.getStateInformation(saved);

    Host restored;
    restored.processor.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    PluginModel restoredModel(restored.processor);
    CHECK(restoredModel.laneCurve(2) == drawn);
    CHECK(restoredModel.laneCurve(0) == defaultLaneCurves()[0]);

    // Loading a factory preset replaces the drawn curve with that preset's:
    // its own where it draws one, the default where it does not.
    const auto presets = restoredModel.presets();
    const auto load = [&](const std::string& name) {
        const auto found = std::find_if(presets.begin(), presets.end(), [&](const auto& preset) { return preset.name == name; });
        REQUIRE(found != presets.end());
        restoredModel.loadPreset(static_cast<int>(found - presets.begin()));
    };
    load("Braided Cable");
    CHECK(restoredModel.get(laneParam(2, LaneField::shape)) == static_cast<float>(LaneShape::custom));
    CHECK_FALSE(restoredModel.laneCurve(2) == drawn);
    CHECK_FALSE(restoredModel.laneCurve(2) == defaultLaneCurves()[2]);
    load("Coupled Reese");
    CHECK(restoredModel.laneCurve(2) == defaultLaneCurves()[2]);
}


#if JUCE_MAC
TEST_CASE("Apple libmalloc counts no audio callback allocations for note and SysEx events") {
    Host host;
    const auto allocationsFor = [&](const juce::MidiMessage& message) {
        juce::MidiBuffer midi;
        midi.addEvent(message, 7);
        host.buffer.clear();
        const AllocationCount allocations;
        host.processor.processBlock(host.buffer, midi);
        return allocations.get();
    };
    const std::vector<juce::uint8> payload(200, 0x01);
    const auto sysex = juce::MidiMessage::createSysExMessage(payload.data(), static_cast<int>(payload.size()));
    {
        // The counter does count: copying a long message is one allocation.
        const AllocationCount allocations;
        const juce::MidiMessage copy(sysex);
        CHECK(allocations.get() == 1);
    }
    CHECK(allocationsFor(juce::MidiMessage::noteOn(1, 29, 1.0f)) == 0);
    CHECK(allocationsFor(sysex) == 0);
}
#endif

TEST_CASE("Quality set from another thread takes effect on the message thread") {
    const auto setQualityElsewhere = [](PluginProcessor& processor, float normalised) {
        std::thread([&] { processor.state.getParameter("quality")->setValueNotifyingHost(normalised); }).join();
    };
    {
        Host host;
        const int before = host.processor.getLatencySamples();
        setQualityElsewhere(host.processor, 0.0f);
        CHECK(host.processor.getLatencySamples() == before);
        runMessageLoop();
        CHECK(host.processor.getLatencySamples() != before);
    }
    {
        // A processor destroyed with a change still queued leaves nothing behind to run.
        const juce::ScopedJuceInitialiser_GUI juce;
        {
            PluginProcessor processor;
            processor.setPlayConfigDetails(0, 2, kSampleRate, kBlock);
            processor.prepareToPlay(kSampleRate, kBlock);
            setQualityElsewhere(processor, 1.0f);
        }
        runMessageLoop();
    }
}

TEST_CASE("About identifies the studio, links to this synth and returns to the same preset") {
    Host host;
    const int preset = host.processor.getCurrentProgram();
    PluginEditor editor(host.processor);
    CHECK(juce::String(JucePlugin_Manufacturer) == "Human Being Audio");
    CHECK(JucePlugin_ManufacturerCode == 0x48626d75);
    CHECK(JucePlugin_PluginCode == 0x5472666d);
    auto* sheet = editor.findChildWithID("aboutSheet");
    REQUIRE(sheet != nullptr);
    CHECK_FALSE(sheet->isVisible());
    editor.showAbout();
    REQUIRE(sheet->isVisible());
    CHECK(sheet->getTitle() == "About Tract Form");
    juce::Image header(juce::Image::ARGB, 128, 64, true, juce::SoftwareImageType{});
    {
        juce::Graphics graphics(header);
        sheet->paintOverChildren(graphics);
    }
    const auto mark = header.getPixelAt(35, 24);
    CHECK(mark.getBlue() > mark.getRed());
    auto* source = dynamic_cast<juce::HyperlinkButton*>(sheet->findChildWithID("aboutSource"));
    REQUIRE(source != nullptr);
    CHECK(source->getURL().toString(false) == "https://github.com/seandearnaley/human-being-audio/tree/main/products/tract-form");
    CHECK(sheet->getDescription().contains("AGPL"));
    CHECK(sheet->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    CHECK_FALSE(sheet->isVisible());
    CHECK(host.processor.getCurrentProgram() == preset);
}
