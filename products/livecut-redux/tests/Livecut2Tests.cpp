// SPDX-License-Identifier: GPL-3.0-or-later

#include "OutputMixer.h"
#include "Parameters.h"
#include "PluginModel.h"
#include "PluginProcessor.h"
#include "lc/ui/fake_model.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <memory>
#include <numbers>

namespace {

class TestPlayHead final : public juce::AudioPlayHead {
  public:
    void set(double bpm, double ppq, bool playing = true) {
        bpm_ = bpm;
        ppq_ = ppq;
        playing_ = playing;
    }

    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo info;
        info.setIsPlaying(playing_);
        info.setBpm(bpm_);
        info.setTimeSignature(TimeSignature{4, 4});
        info.setPpqPosition(ppq_);
        info.setPpqPositionOfLastBarStart(std::floor(ppq_ / 4.0) * 4.0);
        return info;
    }

  private:
    double bpm_ = 140.0;
    double ppq_ = 0.0;
    bool playing_ = true;
};

void setParam(LivecutAudioProcessor& proc, const char* id, float realValue) {
    auto* p = proc.getParameterState().getParameter(id);
    REQUIRE(p != nullptr);
    p->setValueNotifyingHost(p->convertTo0to1(realValue));
}

} // namespace

TEST_CASE("OutputMixer Sub-Anchor preserves mono sub-bass while letting upper spectrum stay wet", "[v2][sub-anchor]") {
    livecut::OutputMixer mixer;
    mixer.prepare(48000.0, 1.0f, 0.0f, 120.0f, false);

    double subLeftEnergy = 0.0;
    double subDiffEnergy = 0.0;
    constexpr int total = 4800;
    for (int i = 0; i < total; ++i) {
        const float dryL = std::sin(2.0f * std::numbers::pi_v<float> * 50.0f * static_cast<float>(i) / 48000.0f);
        const float dryR = 0.0f; // hard-panned 50 Hz sub should anchor mono!
        float outL = 0.0f;       // muted wet path
        float outR = 0.0f;
        mixer.process(dryL, dryR, outL, outR);
        if (i >= 1200) {
            subLeftEnergy += static_cast<double>(outL) * outL;
            const double diff = static_cast<double>(outL - outR);
            subDiffEnergy += diff * diff;
        }
    }

    CHECK(subLeftEnergy > 200.0);
    CHECK(subDiffEnergy < 1.0e-6);
}

TEST_CASE("OutputMixer Safety Limiter is unity below knee and caps hot peaks cleanly", "[v2][limiter]") {
    livecut::OutputMixer mixer;
    mixer.prepare(48000.0, 1.0f, 0.0f, 0.0f, true);

    // Below knee (0.5 < 0.88): strictly identity after smoother settles
    float outL = 0.5f, outR = -0.4f;
    for (int i = 0; i < 1200; ++i) {
        outL = 0.5f;
        outR = -0.4f;
        mixer.process(0.5f, -0.4f, outL, outR);
    }
    CHECK(outL == Catch::Approx(0.5f).margin(1.0e-5f));
    CHECK(outR == Catch::Approx(-0.4f).margin(1.0e-5f));
    CHECK(mixer.gainReductionDb() == Catch::Approx(0.0f).margin(1.0e-4f));

    // Hot signal (+12 dB = 4.0): bounded strictly <= 0.985 and reports positive GR dB
    float hotL = 4.0f, hotR = -2.0f;
    mixer.process(0.0f, 0.0f, hotL, hotR);
    CHECK(std::abs(hotL) <= 0.986f);
    CHECK(std::abs(hotR) <= 0.986f);
    CHECK(mixer.gainReductionDb() > 6.0f);
}

TEST_CASE("All 28 factory presets load cleanly and produce finite stereo audio", "[v2][presets]") {
    LivecutAudioProcessor proc;
    proc.prepareToPlay(48000.0, 256);

    const auto& bank = proc.getPresets().all();
    REQUIRE(bank.size() >= 28);
    CHECK(proc.getNumPrograms() == static_cast<int>(bank.size()));

    TestPlayHead head;
    proc.setPlayHead(&head);
    juce::MidiBuffer midi;

    for (int i = 0; i < static_cast<int>(bank.size()); ++i) {
        proc.setCurrentProgram(i);
        CHECK(proc.getProgramName(i).toStdString() == bank[static_cast<std::size_t>(i)].name);

        juce::AudioBuffer<float> buf(2, 256);
        for (int s = 0; s < 256; ++s) {
            const float v = 0.4f * std::sin(0.08f * static_cast<float>(s));
            buf.setSample(0, s, v);
            buf.setSample(1, s, -v);
        }
        head.set(140.0, static_cast<double>(i) * 0.25, true);
        proc.processBlock(buf, midi);

        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < 256; ++s)
                REQUIRE(std::isfinite(buf.getSample(ch, s)));
    }
}

TEST_CASE("MIDI C1-G1 notes trigger Pad programs, CC1/CC74 drive XY, and CC123 releases", "[v2][midi]") {
    LivecutAudioProcessor proc;
    proc.prepareToPlay(48000.0, 256);
    CHECK(proc.acceptsMidi());
    setParam(proc, livecut::ids::surface, 0.0f); // Pad

    TestPlayHead head;
    proc.setPlayHead(&head);

    // Note D#1 (MIDI 39 = index 3 = Suction) + CC1 (X) + CC74 (Y)
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 39, 0.9f), 0);
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 1, 100), 0);
    midi.addEvent(juce::MidiMessage::controllerEvent(1, 74, 80), 0);

    juce::AudioBuffer<float> buf(2, 256);
    buf.clear();
    head.set(120.0, 0.0, true);
    proc.processBlock(buf, midi);

    CHECK(proc.getDisplayState().padEngaged.load());
    CHECK(proc.getDisplayState().padActiveProgram.load() == 3);
    CHECK((proc.getDisplayState().midiPadMask.load() & (1u << 3)) != 0);

    // CC123 All Notes Off releases held MIDI pad mask
    juce::MidiBuffer panic;
    panic.addEvent(juce::MidiMessage::controllerEvent(1, 123, 0), 0);
    head.set(120.0, 0.25, true);
    proc.processBlock(buf, panic);
    CHECK(proc.getDisplayState().midiPadMask.load() == 0);
}

TEST_CASE("Processor-owned Undo/Redo, A/B compare, and Roll Seed work reliably", "[v2][history]") {
    LivecutAudioProcessor proc;
    setParam(proc, livecut::ids::seed, 4.0f);
    proc.pushUndoSnapshot();

    setParam(proc, livecut::ids::seed, 11.0f);
    CHECK(proc.canUndo());

    proc.undo();
    CHECK(proc.getParameterState().getRawParameterValue(livecut::ids::seed)->load() == Catch::Approx(4.0f));
    CHECK(proc.canRedo());

    proc.redo();
    CHECK(proc.getParameterState().getRawParameterValue(livecut::ids::seed)->load() == Catch::Approx(11.0f));

    // A/B slot compare
    CHECK_FALSE(proc.isSlotB());
    proc.toggleAB();
    CHECK(proc.isSlotB());
    setParam(proc, livecut::ids::seed, 15.0f);
    proc.toggleAB();
    CHECK_FALSE(proc.isSlotB());
    CHECK(proc.getParameterState().getRawParameterValue(livecut::ids::seed)->load() == Catch::Approx(11.0f));

    // Roll Seed changes seed to a fresh valid value
    const int before =
        static_cast<int>(std::lround(proc.getParameterState().getRawParameterValue(livecut::ids::seed)->load()));
    proc.rollSeed();
    const int after =
        static_cast<int>(std::lround(proc.getParameterState().getRawParameterValue(livecut::ids::seed)->load()));
    CHECK(after != before);
    CHECK(after >= 1);
    CHECK(after <= 16);
}

TEST_CASE("Mono-to-Stereo bus layout processes cleanly into both stereo output channels", "[v2][buses]") {
    LivecutAudioProcessor proc;
    juce::AudioProcessor::BusesLayout monoToStereo;
    monoToStereo.inputBuses.add(juce::AudioChannelSet::mono());
    monoToStereo.outputBuses.add(juce::AudioChannelSet::stereo());
    CHECK(proc.isBusesLayoutSupported(monoToStereo));
    REQUIRE(proc.setBusesLayout(monoToStereo));

    proc.prepareToPlay(48000.0, 128);
    juce::AudioBuffer<float> buf(2, 128);
    buf.clear();
    for (int i = 0; i < 128; ++i)
        buf.setSample(0, i, 0.5f * std::sin(0.1f * static_cast<float>(i)));

    juce::MidiBuffer midi;
    proc.processBlock(buf, midi);

    double rEnergy = 0.0;
    for (int i = 0; i < 128; ++i)
        rEnergy += static_cast<double>(buf.getSample(1, i)) * buf.getSample(1, i);
    CHECK(rEnergy > 1.0);
}

TEST_CASE("One-drag WAV capture exports a valid stereo 32-bit float WAV file", "[v2][capture]") {
    LivecutAudioProcessor proc;
    setParam(proc, livecut::ids::surface, 1.0f); // Classic
    setParam(proc, livecut::ids::minPhraseBars, 1.0f);
    setParam(proc, livecut::ids::maxPhraseBars, 1.0f);
    proc.prepareToPlay(8000.0, 400);

    TestPlayHead head;
    proc.setPlayHead(&head);
    juce::MidiBuffer midi;
    double ppq = 0.0;
    for (int block = 0; block < 45; ++block) {
        juce::AudioBuffer<float> buf(2, 400);
        for (int s = 0; s < 400; ++s) {
            const float x = 0.35f * std::sin(0.07f * static_cast<float>(block * 400 + s));
            buf.setSample(0, s, x);
            buf.setSample(1, s, -x);
        }
        head.set(120.0, ppq, true);
        proc.processBlock(buf, midi);
        ppq += 400.0 * 120.0 / 60.0 / 8000.0;
    }

    const auto wavFile = proc.getCaptureService().prepareDragWav("TestPhrase");
    REQUIRE(wavFile.existsAsFile());
    CHECK(wavFile.getSize() > 1024);

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(wavFile));
    REQUIRE(reader != nullptr);
    CHECK(reader->numChannels == 2);
    CHECK(reader->bitsPerSample == 32);
    CHECK(reader->lengthInSamples > 4000);
    wavFile.deleteFile();
}

TEST_CASE("PluginModel and FakeModel expose all studio controls and telemetry", "[v2][ui]") {
    LivecutAudioProcessor proc;
    proc.prepareToPlay(48000.0, 256);
    livecut::PluginModel model(proc);

    CHECK(model.presets().size() >= 28);
    CHECK_FALSE(model.presetName().empty());
    CHECK_FALSE(model.formatValue(livecut::ids::subAnchor).empty());
    CHECK(model.choices(livecut::ids::procedure).size() == 4);

    model.beginGesture(livecut::ids::padX);
    model.setNormalised(livecut::ids::padX, 0.77f);
    model.endGesture(livecut::ids::padX);
    CHECK(model.getNormalised(livecut::ids::padX) == Catch::Approx(0.77f).margin(0.01f));

    lc::ui::FakeModel fake;
    fake.refreshVisuals();
    CHECK(fake.visuals().cutting);
    CHECK(fake.visuals().waveform[0] > 0.0f);
}
