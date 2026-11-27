#include "PluginModel.h"
#include "PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

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

TEST_CASE("PoleField ships 42 loudness-calibrated factory presets across all 7 categories", "[plugin]") {
    juce::ScopedJuceInitialiser_GUI juceInit;

    pf::PluginProcessor proc;
    proc.prepareToPlay(48000.0, 256);

    const auto& all = proc.presets.all();
    REQUIRE(all.size() >= 42);

    std::vector<std::string> categories;
    for (const auto& p : all)
        if (std::find(categories.begin(), categories.end(), p.category) == categories.end())
            categories.push_back(p.category);
    REQUIRE(categories.size() == 7);

    // Rich harmonic neuro bass + mid-air benchmark signal (0.5 s)
    constexpr int total = 24000;
    std::vector<float> srcL(total), srcR(total);
    for (int i = 0; i < total; ++i) {
        const double t = static_cast<double>(i) / 48000.0;
        double sum = 0.0;
        for (int h = 1; h <= 24; ++h)
            sum += (0.28 / static_cast<double>(h)) * std::sin(2.0 * 3.141592653589793 * 55.0 * h * t);
        srcL[static_cast<std::size_t>(i)] = static_cast<float>(sum);
        srcR[static_cast<std::size_t>(i)] =
            static_cast<float>(sum * 0.96 + 0.04 * std::sin(2.0 * 3.141592653589793 * 443.0 * t));
    }

    double minRmsDb = 100.0;
    double maxRmsDb = -100.0;
    for (std::size_t idx = 0; idx < all.size(); ++idx) {
        proc.prepareToPlay(48000.0, 256);
        proc.presets.load(static_cast<int>(idx));

        juce::AudioBuffer<float> block(2, 256);
        juce::MidiBuffer midi;
        double energy = 0.0;
        int counted = 0;
        for (int pos = 0; pos + 256 <= total; pos += 256) {
            for (int i = 0; i < 256; ++i) {
                block.setSample(0, i, srcL[static_cast<std::size_t>(pos + i)]);
                block.setSample(1, i, srcR[static_cast<std::size_t>(pos + i)]);
            }
            proc.processBlock(block, midi);
            for (int i = 0; i < 256; ++i) {
                const float l = block.getSample(0, i);
                const float r = block.getSample(1, i);
                REQUIRE(std::isfinite(l));
                REQUIRE(std::isfinite(r));
                if (pos >= total / 3) {
                    energy += 0.5 * (static_cast<double>(l) * l + static_cast<double>(r) * r);
                    ++counted;
                }
            }
        }
        const double rmsDb = 10.0 * std::log10(std::max(energy / std::max(1, counted), 1e-12));
        INFO("Preset [" << idx << "] " << all[idx].name << " (" << all[idx].category << ") rmsDb=" << rmsDb);
        minRmsDb = std::min(minRmsDb, rmsDb);
        maxRmsDb = std::max(maxRmsDb, rmsDb);
        CHECK(rmsDb > -22.0);
        CHECK(rmsDb < -5.0);
    }
    INFO("Factory preset RMS spread: min=" << minRmsDb << " dB, max=" << maxRmsDb
                                           << " dB, span=" << (maxRmsDb - minRmsDb));
    CHECK((maxRmsDb - minRmsDb) < 7.0);
}

TEST_CASE("PoleField supports sidechain bus layout and 1-click Live Capture multi-corner imprint", "[plugin]") {
    juce::ScopedJuceInitialiser_GUI juceInit;

    pf::PluginProcessor proc;
    // Check stereo main + stereo sidechain bus layout support
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add(juce::AudioChannelSet::stereo());
    layout.inputBuses.add(juce::AudioChannelSet::stereo());
    layout.outputBuses.add(juce::AudioChannelSet::stereo());
    REQUIRE(proc.isBusesLayoutSupported(layout));
    REQUIRE(proc.setBusesLayout(layout));

    proc.prepareToPlay(48000.0, 256);

    // Feed 8 blocks of rich audio so preIn ring buffer fills
    juce::AudioBuffer<float> buf(4, 256);
    juce::MidiBuffer midi;
    for (int b = 0; b < 8; ++b) {
        for (int i = 0; i < 256; ++i) {
            const double t = static_cast<double>(b * 256 + i) / 48000.0;
            const float s = static_cast<float>(0.5 * std::sin(2.0 * 3.141592653589793 * 900.0 * t) +
                                               0.35 * std::sin(2.0 * 3.141592653589793 * 2200.0 * t));
            buf.setSample(0, i, s);
            buf.setSample(1, i, s);
            buf.setSample(2, i, s);
            buf.setSample(3, i, s);
        }
        proc.processBlock(buf, midi);
    }

    pf::PluginModel model(proc);
    model.refreshVisuals();
    model.captureLiveImprint(1); // Capture into Corner B
    REQUIRE(model.imprint().slotEnabled(1));
    REQUIRE(model.imprintSlotLabel(1) == "Live B");
    REQUIRE(static_cast<int>(model.get(pf::ParamId::cube) + 0.5f) == pf::kNumCubes - 1);

    // Undo restores previous clean state
    model.undo();
    CHECK_FALSE(model.imprint().slotEnabled(1));
}

TEST_CASE("Pole Field host bypass nulls against dry audio delayed by the reported latency", "[plugin][bypass]") {
    juce::ScopedJuceInitialiser_GUI gui;
    pf::PluginProcessor proc;
    auto* quality = proc.state.getParameter("quality");
    REQUIRE(quality != nullptr);
    for (double rate : {44100.0, 96000.0}) {
        for (float q : {0.0f, 0.5f, 1.0f}) {
            quality->setValueNotifyingHost(q);
            proc.prepareToPlay(rate, 64);
            const int latency = proc.getLatencySamples();
            REQUIRE(latency > 0);
            juce::MidiBuffer midi;
            juce::AudioBuffer<float> active(2, 64);
            active.clear();
            active.setSample(0, 63, 0.7f);
            active.setSample(1, 63, -0.4f);
            proc.processBlock(active, midi);
            juce::AudioBuffer<float> bypass(2, latency + 64);
            bypass.clear();
            proc.processBlockBypassed(bypass, midi);
            bypass.addSample(0, latency - 1, -0.7f);
            bypass.addSample(1, latency - 1, 0.4f);
            CAPTURE(rate, q, latency);
            CHECK(bypass.getMagnitude(0, bypass.getNumSamples()) == 0.0f);
        }
    }
}
