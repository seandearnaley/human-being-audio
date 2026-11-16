#include "PluginModel.h"
#include "PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

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
