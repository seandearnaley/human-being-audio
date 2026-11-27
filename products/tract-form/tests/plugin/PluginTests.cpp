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
#include <atomic>
#include <pthread.h>
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
// Counts this thread's allocations while one is alive. Avoids thread_local inside
// malloc_logger because Darwin allocates TLS lazily via malloc on background threads.
class AllocationCount {
  public:
    AllocationCount() {
        owner_.store(pthread_self(), std::memory_order_release);
        count_.store(0, std::memory_order_relaxed);
        active_.store(true, std::memory_order_release);
        malloc_logger = [](uint32_t type, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uint32_t) {
            constexpr uint32_t allocation = 2;
            if ((type & allocation) != 0 && active_.load(std::memory_order_acquire) &&
                pthread_equal(pthread_self(), owner_.load(std::memory_order_acquire))) {
                count_.fetch_add(1, std::memory_order_relaxed);
            }
        };
    }
    ~AllocationCount() {
        active_.store(false, std::memory_order_release);
        malloc_logger = nullptr;
    }
    int get() const { return count_.load(std::memory_order_relaxed); }

  private:
    static std::atomic<bool> active_;
    static std::atomic<pthread_t> owner_;
    static std::atomic<int> count_;
};
std::atomic<bool> AllocationCount::active_{false};
std::atomic<pthread_t> AllocationCount::owner_{};
std::atomic<int> AllocationCount::count_{0};
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
    model.refreshVisuals(); // drain the start-up
    host.run(40);
    model.refreshVisuals();
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
        // Escape the raw pointer via volatile so Release LTO cannot elide the heap copy.
        const AllocationCount allocations;
        const juce::MidiMessage copy(sysex);
        const auto* volatile escaped = copy.getRawData();
        CHECK(escaped != nullptr);
        CHECK(allocations.get() == 1);
    }
    CHECK(allocationsFor(juce::MidiMessage::noteOn(1, 29, 1.0f)) == 0);
    CHECK(allocationsFor(juce::MidiMessage::controllerEvent(1, 1, 96)) == 0);
    CHECK(allocationsFor(juce::MidiMessage::controllerEvent(1, 74, 110)) == 0);
    CHECK(allocationsFor(juce::MidiMessage::channelPressureChange(1, 88)) == 0);
    CHECK(allocationsFor(juce::MidiMessage::controllerEvent(1, 101, 0)) == 0);
    CHECK(allocationsFor(juce::MidiMessage::controllerEvent(1, 100, 0)) == 0);
    CHECK(allocationsFor(juce::MidiMessage::controllerEvent(1, 6, 12)) == 0);
    CHECK(allocationsFor(sysex) == 0);
}
#endif

TEST_CASE("PluginProcessor applies RPN 00 00 pitch bend range and MIDI expression controllers") {
    Host host;
    const auto renderBlock = [&](juce::MidiBuffer& midi) {
        host.buffer.clear();
        host.processor.processBlock(host.buffer, midi);
    };

    // Configure RPN 00 00 = 12 semitones, play A2 (45), bend full up (+12 st -> A3 = 220 Hz).
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 101, 0), 0);
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 100, 0), 0);
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 6, 12), 0);
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 1, 100), 0);
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 74, 120), 0);
    midi.addEvent(juce::MidiMessage::channelPressureChange(1, 96), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 45, 1.0f), 0);
    midi.addEvent(juce::MidiMessage::pitchWheel(1, 16383), 0);
    renderBlock(midi);

    juce::MidiBuffer empty;
    for (int i = 0; i < 40; ++i)
        renderBlock(empty);

    tf::ui::Visuals visuals;
    host.processor.analysis.update(visuals, 0.016f);
    CHECK(12.0 * std::log2(static_cast<double>(visuals.hz) / 110.0) == Catch::Approx(12.0).margin(0.15));
    CHECK(visuals.effActive);
    CHECK(visuals.effDrive > host.processor.currentParams()[ParamId::drive]);
    CHECK(visuals.effMorph > host.processor.currentParams()[ParamId::morph]);

    // CC121 Reset All Controllers returns pitch bend to 0 st (A2 = 110 Hz).
    juce::MidiBuffer resetMidi;
    resetMidi.addEvent(juce::MidiMessage::controllerEvent(1, 121, 0), 0);
    renderBlock(resetMidi);
    for (int i = 0; i < 40; ++i)
        renderBlock(empty);

    host.processor.analysis.update(visuals, 0.016f);
    CHECK(12.0 * std::log2(static_cast<double>(visuals.hz) / 110.0) == Catch::Approx(0.0).margin(0.15));
}

TEST_CASE("Quality set from another thread takes effect on the message thread") {
    const auto setQualityElsewhere = [](PluginProcessor& processor, float normalised) {
        std::thread([&] { processor.state.getParameter("quality")->setValueNotifyingHost(normalised); }).join();
    };
    {
        Host host;
        const int before = host.processor.engineOversamplingFactor();
        REQUIRE(before == 4);
        setQualityElsewhere(host.processor, 0.0f);
        CHECK(host.processor.engineOversamplingFactor() == before);
        runMessageLoop();
        CHECK(host.processor.engineOversamplingFactor() == 2);
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
    auto* instrument = editor.findChildWithID("instrumentSurface");
    REQUIRE(instrument != nullptr);
    REQUIRE(instrument->isVisible());
    editor.showAbout();
    REQUIRE(sheet->isVisible());
    CHECK_FALSE(instrument->isVisible());
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
    CHECK(instrument->isVisible());
    CHECK(host.processor.getCurrentProgram() == preset);
}

TEST_CASE("PluginModel supports Undo/Redo and A/B compare across params and curves") {
    Host host;
    PluginModel model(host.processor);

    CHECK_FALSE(model.canUndo());
    CHECK_FALSE(model.canRedo());
    CHECK_FALSE(model.isSlotB());

    const float initialCoupling = model.get(ParamId::coupling);
    model.begin(ParamId::coupling);
    model.set(ParamId::coupling, 0.90f);
    model.end(ParamId::coupling);
    CHECK(model.canUndo());
    CHECK(model.get(ParamId::coupling) == Approx(0.90f).margin(1e-3f));

    model.setLaneCurve(0, curves::rampUp());
    CHECK(model.laneCurve(0) == curves::rampUp());

    model.undo();
    CHECK_FALSE(model.laneCurve(0) == curves::rampUp());
    model.undo();
    CHECK(model.get(ParamId::coupling) == Approx(initialCoupling).margin(1e-3f));

    model.redo();
    CHECK(model.get(ParamId::coupling) == Approx(0.90f).margin(1e-3f));

    model.toggleAB();
    CHECK(model.isSlotB());
    model.begin(ParamId::coupling);
    model.set(ParamId::coupling, 0.12f);
    model.end(ParamId::coupling);

    model.toggleAB();
    CHECK_FALSE(model.isSlotB());
    CHECK(model.get(ParamId::coupling) == Approx(0.90f).margin(1e-3f));

    model.copyAB();
    model.toggleAB();
    CHECK(model.isSlotB());
    CHECK(model.get(ParamId::coupling) == Approx(0.90f).margin(1e-3f));

    // Processor owns history so Undo/Redo and A/B survive closing and reopening the editor.
    {
        PluginModel reopened(host.processor);
        CHECK(reopened.isSlotB());
        CHECK(reopened.canUndo());
        reopened.begin(ParamId::coupling);
        reopened.set(ParamId::coupling, 0.33f);
        reopened.end(ParamId::coupling);
        reopened.undo();
        CHECK(reopened.get(ParamId::coupling) == Approx(0.90f).margin(1e-3f));
    }
}

TEST_CASE("PluginProcessor chunks oversized host blocks cleanly and filters Poly AT to active note") {
    Host host;
    juce::AudioBuffer<float> big(2, 5000);
    big.clear();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 29, 1.0f), 0);
    // Poly AT for a DIFFERENT note (40 != 29) should not push coupling above default.
    midi.addEvent(juce::MidiMessage::aftertouchChange(1, 40, 127), 100);
    host.processor.processBlock(big, midi);
    CHECK(big.getMagnitude(0, 5000) > 0.1f);

    PluginModel model(host.processor);
    model.refreshVisuals();
    CHECK(model.visuals().gate);
    CHECK(model.visuals().effCoupling == Approx(host.processor.currentParams()[ParamId::coupling]).margin(0.05f));
}

TEST_CASE("PluginModel audition pill triggers lock-free F1 note and bank exposes all 42 factory presets") {
    Host host;
    PluginModel model(host.processor);
    CHECK(model.presets().size() >= 42);

    model.setAudition(true);
    juce::MidiBuffer midi;
    host.buffer.clear();
    host.processor.processBlock(host.buffer, midi);
    CHECK(host.buffer.getMagnitude(0, host.buffer.getNumSamples()) > 0.05f);
    model.refreshVisuals();
    CHECK(model.visuals().gate);
    CHECK(model.visuals().note == 29);

    model.setAudition(false);
    for (int i = 0; i < 32; ++i) {
        host.buffer.clear();
        host.processor.processBlock(host.buffer, midi);
    }
    model.refreshVisuals();
    CHECK_FALSE(model.visuals().gate);
}

TEST_CASE("tract-form renders its longest release inside the reported host tail", "[plugin][tail]") {
    juce::ScopedJuceInitialiser_GUI gui;
    tf::PluginProcessor proc;
    constexpr int rate = 22050, block = 256;
    proc.setRateAndBufferSizeDetails(rate, block);
    proc.prepareToPlay(rate, block);
    proc.state.getParameter("ampRelease")->setValueNotifyingHost(1.0f);
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
