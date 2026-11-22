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
