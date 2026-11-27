// SPDX-License-Identifier: AGPL-3.0-or-later
// Prism Vein real-time DSP engine: phase-locked LR4 sub bypass, exact unity
// Transient Shield, 4x Hann^2 COLA Stereo STFT (Chord Lattice / Carrier Morph /
// Glass Freeze), 0ms Harmonic Comb bank, two-pass temporally smoothed Bark
// Auto-Tame, oversampled ADAA2 grit, MIDI expression, and 4-lane modulation.
#pragma once

#include "pv/nonblocking.h"
#include "pv/params.h"
#include "pv/preset.h"

#include <hba/dsp/adaa.h>
#include <hba/dsp/biquad.h>
#include <hba/dsp/control.h>
#include <hba/dsp/crossover.h>
#include <hba/dsp/lane_math.h>
#include <hba/dsp/limiter.h>
#include <hba/dsp/oversampling.h>
#include <hba/dsp/random.h>
#include <hba/dsp/stft.h>
#include <hba/dsp/svf.h>
#include <hba/dsp/transient_split.h>

#include <array>
#include <cstdint>

namespace pv {

struct Transport {
    double bpm = 172.0;
    double ppq = 0.0;
    bool playing = false;
};

struct TelemetrySnapshot {
    std::array<float, kSpectrumBins> inSpectrum{};    // normalized 0..1 log-dB input spectrum
    std::array<float, kSpectrumBins> outSpectrum{};   // normalized 0..1 log-dB colored spectrum
    std::array<float, kSpectrumBins> tameReduction{}; // normalized 0..1 Bark auto-tame cut
    std::array<float, 12> pitchEnergy{};              // live glow per pitch class C..B
    std::uint16_t activePitchMask = kDefaultChordMask;
    float peakL = 0.0f;
    float peakR = 0.0f;
    float grDb = 0.0f;
    float transientAmt = 0.0f;
    float tamePeakDb = 0.0f;
    float pitchBendSt = 0.0f;
    float sampleRate = 48000.0f;
    int latencySamples = 1024;
    double ppq = 0.0;
    double notePpq = 0.0;
    double freeSec = 0.0;
    double noteSec = 0.0;
    bool sidechainActive = false;
    bool midiActive = false;
    bool midiWaiting = false;
    bool frozen = false;
};

class Engine {
  public:
    static constexpr int kFftSize = 1024;
    static constexpr int kNumBins = kFftSize / 2 + 1;
    static constexpr int kMaxChordVoices = 12;

    Engine() noexcept = default;

    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept PV_NONBLOCKING;

    void setParams(const Params& params) noexcept PV_NONBLOCKING { targetParams_ = params; }
    void setChordMask(std::uint16_t mask) noexcept PV_NONBLOCKING {
        const auto clean = static_cast<std::uint16_t>(mask & 0x0FFFu);
        chordMask_ = clean != 0u ? clean : kDefaultChordMask;
    }
    void setLaneCurves(const LaneCurves& curves) noexcept PV_NONBLOCKING { curves_ = curves; }
    void setTransport(const Transport& transport) noexcept PV_NONBLOCKING { transport_ = transport; }

    void noteOn(int midiNote, float velocity) noexcept PV_NONBLOCKING;
    void noteOff(int midiNote) noexcept PV_NONBLOCKING;
    void setSustainPedal(bool down) noexcept PV_NONBLOCKING;
    void setPitchBend(float semitones) noexcept PV_NONBLOCKING;
    void setModWheel(float norm01) noexcept PV_NONBLOCKING;
    void resetControllers() noexcept PV_NONBLOCKING;
    void allNotesOff() noexcept PV_NONBLOCKING;

    int latencySamples() const noexcept PV_NONBLOCKING;

    // Zero-allocation, lock-free audio block processing.
    // `sidechainL/R` may be nullptr when no sidechain bus is connected.
    void process(const float* inL, const float* inR, const float* sidechainL, const float* sidechainR, float* outL,
                 float* outR, int numSamples) noexcept PV_NONBLOCKING;

    const TelemetrySnapshot& telemetry() const noexcept PV_NONBLOCKING { return telemetry_; }

  private:
    static constexpr int kCombBufSize = 4096;
    static constexpr int kCombBufMask = kCombBufSize - 1;

    struct CombVoice {
        // Fractional-delay stereo waveguide comb resonator (all harmonics 1..N up to Nyquist).
        std::array<float, kCombBufSize> bufL{};
        std::array<float, kCombBufSize> bufR{};
        int writePos = 0;
        double lpL = 0.0;
        double lpR = 0.0;
        double dcInL = 0.0, dcOutL = 0.0;
        double dcInR = 0.0, dcOutR = 0.0;
        double freqHz = 174.61;
        double targetHz = 174.61;
        float panL = 0.707f;
        float panR = 0.707f;
        float weight = 0.0f;
    };

    void updateActiveChordTargets(const Params& p, double totalShiftSt, bool& midiWaitingOut) noexcept PV_NONBLOCKING;
    void processStftFrame(hba::dsp::StereoStft<kFftSize>::FrameBuffers& fb, const Params& p, double modDecay,
                          double modTame, double modTilt, double modFan, double modMorph,
                          bool publishTelemetry) noexcept PV_NONBLOCKING;
    void processCombSample(double inL, double inR, double decayAmt, double tiltAmt, double fanAmt, double qSharp,
                           double& outL, double& outR) noexcept PV_NONBLOCKING;
    void pushCombTelemetrySample(float inMono, float outMono, double liveTameCutDb,
                                 double tameFocusHz) noexcept PV_NONBLOCKING;
    void synthesizeDemoSample(double& outL, double& outR) noexcept PV_NONBLOCKING;
    void updateTelemetrySpectrum(const float* inMag, const float* outMag,
                                 const float* tameGain) noexcept PV_NONBLOCKING;
    static double justIntervalRatio(int semitonesFromRoot) noexcept PV_NONBLOCKING;

    double sampleRate_ = 48000.0;
    double invSr_ = 1.0 / 48000.0;
    int maxBlockSize_ = 512;

    Params targetParams_ = Params::defaults();
    LaneCurves curves_ = defaultLaneCurves();
    Transport transport_{};
    std::uint16_t chordMask_ = kDefaultChordMask;

    // Live MIDI note & expression state (zero-allocation fixed tables).
    std::array<std::uint8_t, 128> midiHeld_{};
    std::array<std::uint8_t, 128> midiSustained_{};
    std::array<std::uint8_t, 128> midiLatched_{};
    int midiHeldCount_ = 0;
    int midiSustainedCount_ = 0;
    int midiLatchedCount_ = 0;
    bool sustainDown_ = false;
    double lastMidiVelocity_ = 0.85;
    double pitchBendTargetSt_ = 0.0;
    double modWheelTarget_ = 0.0;
    bool prevHoldParam_ = false;

    // Resolved active chord fundamentals (Hz) & glide smoothers.
    std::array<CombVoice, kMaxChordVoices> voices_{};
    int activeVoiceCount_ = 5;
    double voiceGlideCoef_ = 0.02;

    // Clocks for 4-lane modulation.
    double hostPpq_ = 0.0;
    double notePpq_ = 0.0;
    double freeSec_ = 0.0;
    double noteSec_ = 0.0;

    // Sub-safe LR4 crossover (cached coefs) + Unity Transient Shield + exact STFT latency alignment.
    hba::dsp::Lr4Split subSplitL_{};
    hba::dsp::Lr4Split subSplitR_{};
    hba::dsp::SvfCoefs cachedXoverCoefs_{};
    double lastSubHz_ = -1.0;
    hba::dsp::TransientSplit transientSplit_{};
    hba::dsp::DelayLine<double> subDelayL_{};
    hba::dsp::DelayLine<double> subDelayR_{};
    hba::dsp::DelayLine<double> transDelayL_{};
    hba::dsp::DelayLine<double> transDelayR_{};
    hba::dsp::DelayLine<double> dryBodyDelayL_{};
    hba::dsp::DelayLine<double> dryBodyDelayR_{};
    hba::dsp::DelayLine<double> combDelayL_{};
    hba::dsp::DelayLine<double> combDelayR_{};

    // 4x Hann^2 COLA Stereo STFT + spectral memory buffers.
    hba::dsp::StereoStft<kFftSize> stft_{};
    std::array<float, kNumBins> tailReL_{};
    std::array<float, kNumBins> tailImL_{};
    std::array<float, kNumBins> tailReR_{};
    std::array<float, kNumBins> tailImR_{};
    std::array<float, kNumBins> freezeMagL_{};
    std::array<float, kNumBins> freezeMagR_{};
    std::array<float, kNumBins> freezePhaseL_{};
    std::array<float, kNumBins> freezePhaseR_{};
    std::array<float, kNumBins> maskL_{};
    std::array<float, kNumBins> maskR_{};
    std::array<float, kNumBins> workInMag_{};
    std::array<float, kNumBins> workPreTameMag_{};
    std::array<float, kNumBins> workOutMag_{};
    std::array<float, kNumBins> workCarrReL_{};
    std::array<float, kNumBins> workCarrImL_{};
    std::array<float, kNumBins> workCarrReR_{};
    std::array<float, kNumBins> workCarrImR_{};
    std::array<float, kNumBins> workCarrMag_{};
    std::array<float, kNumBins> tameBinSmooth_{};
    std::array<float, kNumBins> workTameGain_{};
    std::array<float, kNumBins> envMain_{};
    std::array<float, kNumBins> envCarr_{};
    std::array<float, kNumBins> internalCarrPhase_{};
    float compGainSmoothed_ = 1.0f;
    float freezeFade_ = 0.0f;
    bool freezeCaptured_ = false;
    bool prevFreezeParam_ = false;

    // Analysis-only zero-latency FFT ring for live Harmonic Comb / Live 0ms spectrum telemetry.
    hba::dsp::RealFft<kFftSize> telFft_{};
    std::array<float, kFftSize> telWin_{};
    std::array<float, kFftSize> telInRing_{};
    std::array<float, kFftSize> telOutRing_{};
    std::array<float, kFftSize> telTimeIn_{};
    std::array<float, kFftSize> telTimeOut_{};
    std::array<float, kFftSize> telScratchRe_{};
    std::array<float, kFftSize> telScratchIm_{};
    std::array<float, kNumBins> telInRe_{};
    std::array<float, kNumBins> telInIm_{};
    std::array<float, kNumBins> telOutRe_{};
    std::array<float, kNumBins> telOutIm_{};
    std::array<float, kNumBins> telInMag_{};
    std::array<float, kNumBins> telOutMag_{};
    std::array<float, kNumBins> telTameGain_{};
    int telPos_ = 0;
    int telHopCount_ = 0;

    // Zero-latency live Auto-Tame resonant de-harsher (used in Live 0ms & Harmonic Comb modes).
    hba::dsp::BiquadState liveTameL_{};
    hba::dsp::BiquadState liveTameR_{};
    double liveTameEnv_ = 0.0;

    // Post-color ADAA2 grit + Air high-shelf + DC blockers + ADAA1 safety ceiling.
    int preparedOsFactor_ = 2;
    hba::dsp::Oversampler osL_{};
    hba::dsp::Oversampler osR_{};
    hba::dsp::MultiShaperAdaa2 shaperL_{};
    hba::dsp::MultiShaperAdaa2 shaperR_{};
    hba::dsp::BiquadState airL_{};
    hba::dsp::BiquadState airR_{};
    hba::dsp::DcBlocker dcL_{};
    hba::dsp::DcBlocker dcR_{};
    hba::dsp::SoftClipAdaa1 clipL_{};
    hba::dsp::SoftClipAdaa1 clipR_{};
    double grEnv_ = 1.0;

    // Parameter & crossfade smoothers.
    hba::dsp::Smoother smInputGain_{};
    hba::dsp::Smoother smOutputGain_{};
    hba::dsp::Smoother smSubGain_{};
    hba::dsp::Smoother smColor_{};
    hba::dsp::Smoother smShield_{};
    hba::dsp::Smoother smGrit_{};
    hba::dsp::Smoother smSubHz_{};
    hba::dsp::Smoother smAirDb_{};
    hba::dsp::Smoother smInHarm_{};
    hba::dsp::Smoother smCombBlend_{};
    hba::dsp::Smoother smMidiGate_{};
    hba::dsp::Smoother smPitchBend_{};
    hba::dsp::Smoother smModWheel_{};

    // Built-in Demo Neuro Reese + Breakbeat Audition Generator.
    double demoPhase1_ = 0.0;
    double demoPhase2_ = 0.0;
    double demoPhaseSub_ = 0.0;
    double demoStepPhase_ = 0.0;
    int demoStep_ = 0;
    double demoKickEnv_ = 0.0;
    double demoKickPhase_ = 0.0;
    double demoSnareEnv_ = 0.0;
    double demoHatEnv_ = 0.0;
    hba::dsp::SvfState demoReeseFilterL_{};
    hba::dsp::SvfState demoReeseFilterR_{};
    hba::dsp::SvfState demoSnareFilter_{};
    hba::dsp::XorShift32 demoRng_{};

    // Telemetry + log-frequency bin mapping for 96-column spectrum display.
    std::array<float, kSpectrumBins> specBinLo_{};
    std::array<float, kSpectrumBins> specBinHi_{};
    double scRms_ = 0.0;
    TelemetrySnapshot telemetry_{};
};

} // namespace pv
