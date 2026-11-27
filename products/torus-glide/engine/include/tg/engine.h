// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "tg/chord_matrix.h"
#include "tg/params.h"

#include <hba/dsp/adaa.h>
#include <hba/dsp/control.h>
#include <hba/dsp/crossover.h>
#include <hba/dsp/limiter.h>
#include <hba/dsp/nonblocking.h>
#include <hba/dsp/oversampling.h>
#include <hba/dsp/svf.h>

#include <array>
#include <cstdint>
#include <vector>

namespace tg {

struct Transport {
    double bpm = 172.0;
    double ppq = 0.0;
    bool playing = false;
};

struct OutMidiEvent {
    std::uint8_t data[3]{0, 0, 0};
    int size = 0;
    int sampleOffset = 0;
};

struct OutMidiBuffer {
    static constexpr int kCapacity = 256;
    std::array<OutMidiEvent, kCapacity> events{};
    int count = 0;

    void clear() noexcept HBA_NONBLOCKING { count = 0; }
    void push(int offset, std::uint8_t b0, std::uint8_t b1, std::uint8_t b2, int size = 3) noexcept HBA_NONBLOCKING {
        if (count >= kCapacity)
            return;
        auto& e = events[static_cast<std::size_t>(count++)];
        e.data[0] = b0;
        e.data[1] = b1;
        e.data[2] = b2;
        e.size = size;
        e.sampleOffset = offset;
    }
};

struct TelemetrySnapshot {
    int mode = 0;
    float barPhase = 0.0f;   // 0..1 within current loopBars
    float spiralPos = 0.0f;  // unwrapped octave phase for smooth spiral visualization
    float morphPos = 0.0f;   // effective 0..1 morph position
    float lane1Value = 0.0f; // -1..1
    float lane2Value = 0.0f; // -1..1
    float lane1Phase = 0.0f; // 0..1
    float lane2Phase = 0.0f; // 0..1
    int activeScene = 0;
    int barNumber = 1;
    int beatNumber = 1;
    float bpm = 172.0f;
    float gateLevel = 1.0f;
    bool sounding = false;
    std::array<float, kVoiceCount> voicePitches{};       // current MIDI pitches of 5 voices
    std::array<float, kOctaveCount> spiralPartialU{};    // 0..7 log-octave positions
    std::array<float, kOctaveCount> spiralPartialGain{}; // 0..1 bell weights
    float peakL = 0.0f;
    float peakR = 0.0f;
    float subPeak = 0.0f;
    float rmsLevel = 0.0f;
    float limiterGain = 1.0f;
    float effectiveBellHz = 320.0f;
    float effectiveFilterHz = 2800.0f;
    float effectiveSpace = 0.28f;
    float effectiveDrive = 0.42f;
    float effectiveSubAnchor = 0.78f;
    float spiralVel = 1.0f;
    float padAmount = 0.0f;
    bool padActive = false;
    int keySwitchScene = -1;
    int heldNoteCount = 0;
    std::array<int, kVoiceCount> heldNotes{};
};

class Engine {
  public:
    void prepare(double sampleRate, int maxBlockSize, int oversamplingFactor = 4);
    void reset() noexcept HBA_NONBLOCKING;

    void setLaneCurves(const LaneCurves& curves) noexcept HBA_NONBLOCKING { curves_ = curves; }
    void setSceneMatrix(const SceneMatrix& matrix) noexcept HBA_NONBLOCKING { matrix_ = matrix; }

    // Real-time MIDI / expression input (audio thread, nonblocking).
    void noteOn(int note, float velocity, bool allowKeySwitch = false) noexcept HBA_NONBLOCKING;
    void noteOff(int note) noexcept HBA_NONBLOCKING;
    void clearKeySwitch() noexcept HBA_NONBLOCKING { keySwitchScene_ = -1; }
    void pitchBend(float semitones) noexcept HBA_NONBLOCKING { pitchBendSt_ = semitones; }
    void setPitchBendRange(float semitones) noexcept HBA_NONBLOCKING { pitchBendRangeSt_ = std::max(1.0f, semitones); }
    void setModWheel(float norm01) noexcept HBA_NONBLOCKING { modWheel_ = std::clamp(norm01, 0.0f, 1.0f); }
    void setTimbre(float norm01) noexcept HBA_NONBLOCKING { timbreCc_ = std::clamp(norm01, 0.0f, 1.0f); }
    void setAftertouch(float norm01) noexcept HBA_NONBLOCKING { aftertouch_ = std::clamp(norm01, 0.0f, 1.0f); }
    void sustain(bool down) noexcept HBA_NONBLOCKING;
    void allNotesOff() noexcept HBA_NONBLOCKING;

    int activeNote() const noexcept HBA_NONBLOCKING { return currentNote_; }
    static constexpr double kSilentEnvelope = 1e-6;
    static constexpr double spaceDrainSeconds(double amount) noexcept { return 0.04 + amount * 1.45; }

    int latencySamples() const noexcept HBA_NONBLOCKING;

    // Main zero-allocation real-time audio + optional MPE MIDI output block.
    void process(const Params& params, const Transport& transport, float* outL, float* outR, int numSamples,
                 OutMidiBuffer* mpeOut = nullptr) noexcept HBA_NONBLOCKING;

    const TelemetrySnapshot& telemetry() const noexcept HBA_NONBLOCKING { return telemetry_; }

  private:
    struct PartialState {
        double phaseA = 0.0;
        double phaseB = 0.0;
        double phaseSub = 0.0;
    };

    struct MpeVoiceState {
        bool active = false;
        int baseNote = 41;
        int lastBend14 = 8192;
        int lastTimbre7 = 64;
    };

    double oscSample(TimbreKind kind, double phase, double inc, double formantRatio, double foldAmt,
                     double pw) const noexcept HBA_NONBLOCKING;
    struct ModeRenderArgs {
        double sr = 48000.0;
        double invSr = 1.0 / 48000.0;
        double rootMidi = 41.0;
        double curSpiralPos = 0.0;
        double ratioSemi = 12.0;
        SpiralChord spiralChord{};
        double curBellCenter = 320.0;
        double bellWidth = 0.6;
        double detuneRatio = 1.0;
        double width = 0.5;
        double voicePitchCoef = 0.01;
        TimbreKind timbre = TimbreKind::reese;
        double formantRatio = 2.0;
        double foldAmt = 0.2;
        double pw = 0.5;
        const std::array<double, kVoiceCount>* targetVoicePitches = nullptr;
        bool captureTelemetry = false;
    };
    void renderSpiralSample(const ModeRenderArgs& a, double& sumBodyL, double& sumBodyR, double& sumSub,
                            double& lastEffectiveBellHz) noexcept HBA_NONBLOCKING;
    void renderCopedentSample(const ModeRenderArgs& a, double& sumBodyL, double& sumBodyR,
                              double& sumSub) noexcept HBA_NONBLOCKING;
    void renderMobiusSample(const ModeRenderArgs& a, double& sumBodyL, double& sumBodyR,
                            double& sumSub) noexcept HBA_NONBLOCKING;
    void emitMpeConfig(OutMidiBuffer& out, int offset, int rangeSt) noexcept HBA_NONBLOCKING;
    void emitMpeAllNotesOff(OutMidiBuffer& out, int offset) noexcept HBA_NONBLOCKING;
    void updateMpeVoices(OutMidiBuffer& out, int offset, const std::array<double, kVoiceCount>& pitches, bool sounding,
                         int rangeSt, double filterNorm) noexcept HBA_NONBLOCKING;

    double sampleRate_ = 48000.0;
    int maxBlock_ = 512;
    int oversamplingFactor_ = 4;

    LaneCurves curves_ = defaultLaneCurves();
    SceneMatrix matrix_ = matrixForPack(0);

    // Note / envelope / expression state
    std::array<bool, 128> keysDown_{};
    bool sustainDown_ = false;
    bool gateOpen_ = false;
    int currentNote_ = 41; // F2 default
    int keySwitchScene_ = -1;
    float velocity_ = 0.9f;
    float pitchBendSt_ = 0.0f;
    float pitchBendRangeSt_ = 2.0f;
    float modWheel_ = 0.0f;
    float timbreCc_ = 0.5f;
    float aftertouch_ = 0.0f;
    double envLevel_ = 0.0;

    // Clocks & smoothers
    double freePpq_ = 0.0;
    double retrigPpqOffset_ = 0.0;
    double lastTransportPpq_ = 0.0;
    bool wasPlaying_ = false;
    double prevMorphCycle_ = 0.0;
    double prevLoopCycles_ = 0.0;
    bool hasPrevLoopCycles_ = false;
    double padSpiralOffset_ = 0.0;
    double padMobiusOffset_ = 0.0;
    int mobiusCycleOffset_ = 0;

    hba::dsp::Smoother rootMidiSmooth_;
    hba::dsp::Smoother morphSmooth_;
    hba::dsp::Smoother filterSmooth_;
    hba::dsp::Smoother driveSmooth_;
    hba::dsp::Smoother spaceSmooth_;
    hba::dsp::Smoother subAnchorSmooth_;
    hba::dsp::Smoother bellCenterSmooth_;
    hba::dsp::Smoother outputGainSmooth_;
    hba::dsp::Smoother padActiveSmooth_;
    hba::dsp::Smoother padXSmooth_;
    hba::dsp::Smoother padYSmooth_;
    hba::dsp::Smoother spiralVelSmooth_;
    hba::dsp::Smoother detuneSmooth_;
    hba::dsp::Smoother widthSmooth_;
    std::array<hba::dsp::Smoother, kVoiceCount> voicePitchSmooth_{};
    std::array<hba::dsp::Ramp, kVoiceCount> voicePitchRamp_{};
    bool voiceRampPrimed_ = false;

    // Fixed-size oscillator partial bank: 5 voices × 7 octave copies
    std::array<std::array<PartialState, kOctaveCount>, kVoiceCount> partials_{};
    double subOscPhase_ = 0.0;

    // Shared HBA DSP stages (all zero-allocation in process!)
    hba::dsp::Lr4Highpass bodyHpL_, bodyHpR_;
    hba::dsp::Lr4Lowpass subLp_;
    hba::dsp::SvfState subWarmthLp_;
    hba::dsp::Oversampler oversamplerL_, oversamplerR_;
    hba::dsp::MultiShaperAdaa2 shaperL_, shaperR_;
    hba::dsp::SvfState filterL_, filterR_;
    hba::dsp::SvfState filter2L_, filter2R_;

    // Upper-band Stereo Ensemble + 4-Tap Hadamard FDN Manifold Reverb (prepared once, zero heap alloc in process!)
    static constexpr int kSpaceBufMask = 8191;
    static constexpr int kSpaceBufSize = kSpaceBufMask + 1;
    std::array<double, kSpaceBufSize> spaceBufL_{};
    std::array<double, kSpaceBufSize> spaceBufR_{};
    int spaceWritePos_ = 0;
    int silentTailSamples_ = 0;
    double spaceLfo1_ = 0.0;
    double spaceLfo2_ = 0.33;
    double spaceLfo3_ = 0.67;

    static constexpr int kFdnLines = 4;
    std::array<std::vector<double>, kFdnLines> fdnBuf_{};
    std::array<int, kFdnLines> fdnPos_{};
    std::array<int, kFdnLines> fdnLen_{1427, 1783, 2111, 2557};
    std::array<double, kFdnLines> fdnLp_{};
    std::array<double, kFdnLines> fdnHp_{};
    std::array<double, kFdnLines> fdnLfo_{0.0, 0.25, 0.50, 0.75};

    hba::dsp::DcBlocker dcL_, dcR_;
    hba::dsp::Limiter limiter_;

    // MPE output state
    bool mpeConfigured_ = false;
    int lastMpeRangeSt_ = 48;
    int mpeThrottleCounter_ = 0;
    std::array<MpeVoiceState, kVoiceCount> mpeVoices_{};

    TelemetrySnapshot telemetry_{};
};

} // namespace tg
