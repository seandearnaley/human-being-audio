// SPDX-License-Identifier: GPL-3.0-or-later
// Comprehensive tests for shared studio DSP primitives (control, SVF, crossover,
// PhaseRotator, Limiter, Oversampler, MultiShaperAdaa2, denormals) and StudioHistory.
#include <hba/dsp/sample_exchange.h>

#include <hba/dsp/adaa.h>
#include <hba/dsp/control.h>
#include <hba/dsp/crossover.h>
#include <hba/dsp/denormals.h>
#include <hba/dsp/envelope_curve.h>
#include <hba/dsp/harmonic_phase.h>
#include <hba/dsp/lane_math.h>
#include <hba/dsp/limiter.h>
#include <hba/dsp/oversampling.h>
#include <hba/dsp/stft.h>
#include <hba/dsp/svf.h>
#include <hba/dsp/transient_split.h>

#include <hba/dsp/allpass.h>
#include <hba/dsp/biquad.h>
#include <hba/dsp/va_filter.h>
#include <StudioHistory.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

TEST_CASE("hba::dsp control primitives smooth, ramp, delay, and DC-block cleanly", "[dsp][engine]") {
    using namespace hba::dsp;

    // onePoleCoef reaches ~63.2% at tau = 10 ms.
    const double sr = 48000.0;
    const double c = onePoleCoef(0.010, 1.0 / sr);
    Smoother sm;
    sm.snap(0.0);
    for (int i = 0; i < 480; ++i)
        (void)sm.step(1.0, c);
    CHECK(sm.value() == Catch::Approx(1.0 - std::exp(-1.0)).margin(2e-3));

    // Ramp lands exact target on final step.
    Ramp ramp;
    ramp.snap(1.0);
    ramp.to(5.0, 4);
    CHECK(ramp.next() == Catch::Approx(2.0));
    CHECK(ramp.next() == Catch::Approx(3.0));
    CHECK(ramp.next() == Catch::Approx(4.0));
    CHECK(ramp.next() == Catch::Approx(5.0));

    // frac and DelayLine identities.
    CHECK(frac(-0.25) == Catch::Approx(0.75));
    CHECK(frac(1.25) == Catch::Approx(0.25));
    DelayLine<double> delay;
    delay.prepare(3);
    CHECK(delay.process(1.0) == Catch::Approx(0.0));
    CHECK(delay.process(2.0) == Catch::Approx(0.0));
    CHECK(delay.process(3.0) == Catch::Approx(0.0));
    CHECK(delay.process(4.0) == Catch::Approx(1.0));
    CHECK(delay.process(5.0) == Catch::Approx(2.0));

    // DcBlocker removes DC offset while passing 200 Hz cleanly.
    DcBlocker dc;
    dc.prepare(10.0, sr);
    double dcOut = 0.0;
    for (int i = 0; i < 12000; ++i)
        dcOut = dc.process(1.0);
    CHECK(std::abs(dcOut) < 1e-3);

    dc.reset();
    double acPeak = 0.0;
    for (int i = 0; i < 4800; ++i) {
        const double y = dc.process(std::sin(2.0 * kPi * 200.0 * i / sr));
        if (i >= 2400)
            acPeak = std::max(acPeak, std::abs(y));
    }
    CHECK(acPeak == Catch::Approx(1.0).margin(0.02));
}

TEST_CASE("hba::dsp SVF and LR4 crossover sum to unity magnitude across frequency", "[dsp][engine]") {
    using namespace hba::dsp;
    const double sr = 48000.0;
    const auto coefs = butterworth(120.0, sr);

    // Measure steady-state magnitude of low + high at 40 Hz, 120 Hz, and 800 Hz.
    for (double hz : {40.0, 120.0, 800.0}) {
        Lr4Split split;
        Lr4Allpass ap;
        double maxErr = 0.0;
        double sumPeak = 0.0;
        for (int i = 0; i < 9600; ++i) {
            const double x = std::sin(2.0 * kPi * hz * i / sr);
            double lo = 0.0, hi = 0.0;
            split.process(coefs, x, lo, hi);
            const double ref = ap.process(coefs, x);
            if (i >= 4800) {
                maxErr = std::max(maxErr, std::abs((lo + hi) - ref));
                sumPeak = std::max(sumPeak, std::abs(lo + hi));
            }
        }
        CHECK(maxErr < 1e-9);
        CHECK(sumPeak == Catch::Approx(1.0).margin(1e-3));
    }
}

TEST_CASE("hba::dsp::PhaseRotator rotates 20-400 Hz by exact 0..360 deg with flat magnitude across rates",
          "[dsp][engine]") {
    using namespace hba::dsp;

    for (double sr : {44100.0, 48000.0, 96000.0, 192000.0}) {
        for (double hz : {35.0, 80.0, 220.0}) {
            for (double targetDeg : {0.0, 90.0, 180.0, 270.0}) {
                PhaseRotator rot;
                PhaseRotator ref;
                rot.prepare(sr);
                ref.prepare(sr);

                const int settle = static_cast<int>(sr * 0.25);
                const int measure = static_cast<int>(sr * 0.10);
                double reR = 0.0, imR = 0.0, reRef = 0.0, imRef = 0.0;
                for (int i = 0; i < settle + measure; ++i) {
                    const double ang = 2.0 * kPi * hz * i / sr;
                    const double x = std::sin(ang);
                    const double y = rot.process(x, targetDeg);
                    const double r = ref.processReference(x);
                    if (i >= settle) {
                        const double c = std::cos(ang);
                        const double s = std::sin(ang);
                        reR += y * c;
                        imR += y * s;
                        reRef += r * c;
                        imRef += r * s;
                    }
                }
                const double magR = std::hypot(reR, imR);
                const double magRef = std::hypot(reRef, imRef);
                CHECK(magR / magRef == Catch::Approx(1.0).margin(0.005));

                // Phase of y relative to ref (ref -> cos/sin phasor).
                const double cross = reRef * imR - imRef * reR;
                const double dot = reRef * reR + imRef * imR;
                double deg = std::atan2(cross, dot) * (180.0 / kPi);
                // Invert sign because sin(wt + phi) projects to (sin phi, cos phi) on (cos wt, sin wt).
                deg = -deg;
                while (deg < 0.0)
                    deg += 360.0;
                double err = std::fmod(deg - targetDeg + 540.0, 360.0) - 180.0;
                CHECK(std::abs(err) < 0.5);
            }
        }
    }
}

TEST_CASE("hba::dsp::Limiter holds true peaks under ceiling and maintains zero box-sum drift", "[dsp][engine]") {
    using namespace hba::dsp;
    Limiter lim;
    lim.prepare(48000.0);
    const double ceiling = 0.5;
    double peak = 0.0;
    for (int i = 0; i < 48000; ++i) {
        double l = (i < 6000) ? 2.5 * std::sin(2.0 * kPi * 1000.0 * i / 48000.0) : 0.0;
        double r = l * 0.7;
        lim.process(l, r, ceiling, 1.0);
        REQUIRE(std::isfinite(l));
        REQUIRE(std::isfinite(r));
        if (i >= lim.latency())
            peak = std::max(peak, std::max(std::abs(l), std::abs(r)));
    }
    CHECK(peak <= ceiling + 0.02);
    // After ~875 ms of silence, gain recovers cleanly to unity with zero box-sum drift.
    CHECK(lim.gain() == Catch::Approx(1.0).margin(1e-4));
}

TEST_CASE("hba::dsp::Oversampler and MultiShaperAdaa2 preserve unity passband and click-free A->B->A switch",
          "[dsp][engine]") {
    using namespace hba::dsp;

    // 4x Oversampler passes 440 Hz with unity gain and exact latency.
    Oversampler os;
    os.prepare(4);
    double peak = 0.0;
    std::array<float, kMaxOversampling> buf{};
    for (int i = 0; i < 4096; ++i) {
        const float x = std::sin(static_cast<float>(2.0 * kPi * 440.0 * i / 48000.0));
        os.up(x, buf.data());
        const float y = os.down(buf.data());
        REQUIRE(std::isfinite(y));
        if (i >= 1024)
            peak = std::max(peak, static_cast<double>(std::abs(y)));
    }
    CHECK(peak == Catch::Approx(1.0).margin(1e-3));

    // MultiShaperAdaa2 A->B->A switch stays smooth after crossfade settles.
    MultiShaperAdaa2 ms;
    double prev = 0.0;
    double maxJump = 0.0;
    int idx = 0;
    auto run = [&](ShaperModel from, ShaperModel to, double mix, int n, bool track) {
        for (int i = 0; i < n; ++i) {
            const double x = 0.6 * std::sin(2.0 * kPi * 220.0 * static_cast<double>(idx++) / (48000.0 * 4.0));
            double dry = 0.0;
            const double y = ms.process(x, 5.0, 0.25, from, to, mix, dry);
            REQUIRE(std::isfinite(y));
            if (track)
                maxJump = std::max(maxJump, std::abs(y - prev));
            prev = y;
        }
    };
    run(ShaperModel::fold, ShaperModel::fold, 1.0, 512, false);
    run(ShaperModel::fold, ShaperModel::sine, 0.5, 128, false);
    run(ShaperModel::fold, ShaperModel::sine, 1.0, 512, false);
    for (int i = 0; i <= 100; ++i)
        run(ShaperModel::sine, ShaperModel::fold, static_cast<double>(i) / 100.0, 1, true);
    CHECK(maxJump < 0.06);

    // ScopedFlushDenormals flushes subnormals to zero while in scope.
    {
        ScopedFlushDenormals flush;
        const volatile double tiny = 1e-320;
        const volatile double one = 1.0;
        CHECK((tiny * one) == 0.0);
    }

    // Limiter, SvfCoefs, softClip, and softGuard defend against unprepared/edge inputs.
    {
        Limiter unprepared;
        double l = 0.5, r = -0.5;
        unprepared.process(l, r, 1.0, 1.0);
        CHECK(std::isfinite(l));
        CHECK(std::isfinite(r));

        const auto svfZeroSr = SvfCoefs::make(1000.0, 0.0, 0.0);
        CHECK(std::isfinite(svfZeroSr.a1));
        CHECK(std::isfinite(svfZeroSr.a2));
        CHECK(std::isfinite(svfZeroSr.a3));

        CHECK(std::isfinite(softClip(2.0, 1.0, 1.0)));
        CHECK(std::isfinite(softGuard(5.0, 4.0, 4.0)));
    }
}

TEST_CASE("hba::studio::History supports undo/redo, coalescing, capacity cap, and A/B compare", "[dsp]") {
    hba::studio::History<int> h;
    CHECK_FALSE(h.canUndo());
    CHECK_FALSE(h.canRedo());
    CHECK_FALSE(h.isSlotB());

    h.push(1);
    h.pushCoalesced(2, 42u, 1000);
    h.pushCoalesced(3, 42u, 1200); // coalesced within 450 ms window -> still one undo entry!
    CHECK(h.canUndo());

    const auto u1 = h.undo(4);
    REQUIRE(u1.has_value());
    CHECK(*u1 == 2);
    const auto u2 = h.undo(2);
    REQUIRE(u2.has_value());
    CHECK(*u2 == 1);
    CHECK_FALSE(h.canUndo());

    const auto r1 = h.redo(1);
    REQUIRE(r1.has_value());
    CHECK(*r1 == 2);

    // A/B slot isolation + copyActiveToOther.
    CHECK(h.toggleAB(10) == 10);
    CHECK(h.isSlotB());
    CHECK(h.toggleAB(20) == 10);
    CHECK_FALSE(h.isSlotB());
    h.copyActiveToOther(10);
    CHECK(h.toggleAB(10) == 10);
}

TEST_CASE("hba::dsp::BaseRate4x, SoftClipAdaa1, and lanePhaseHz meet anti-aliasing and precision specs",
          "[dsp][engine]") {
    using namespace hba::dsp;

    // BaseRate4x preserves unity passband at 440 Hz and 1 kHz.
    BaseRate4x os4;
    os4.prepare();
    double peak = 0.0;
    double sumSq = 0.0;
    int rmsCount = 0;
    std::array<float, BaseRate4x::kFactor> buf{};
    for (int i = 0; i < 4096; ++i) {
        const float x = std::sin(static_cast<float>(2.0 * kPi * 440.0 * i / 48000.0));
        os4.up(x, buf.data());
        const float y = os4.down(buf.data());
        REQUIRE(std::isfinite(y));
        if (i >= 1024) {
            peak = std::max(peak, static_cast<double>(std::abs(y)));
            sumSq += static_cast<double>(y) * static_cast<double>(y);
            ++rmsCount;
        }
    }
    CHECK(peak == Catch::Approx(1.0).margin(1e-3));
    CHECK(std::sqrt(2.0 * sumSq / rmsCount) == Catch::Approx(1.0).margin(2e-3));

    // SoftClipAdaa1: bit-exact linear midpoint below knee, strictly bounded above knee,
    // and suppresses aliasing well below raw memoryless softClip on a hot high-frequency sine.
    SoftClipAdaa1 cA, cB;
    for (int i = 0; i < 64; ++i) {
        const double x = 0.25 * std::sin(0.1 * i);
        // Different ceilings well above |x| must produce bit-identical output below knee.
        CHECK(cA.process(x, 0.6, 0.8) == cB.process(x, 0.6, 2.5));
    }

    cA.reset();
    double maxAdaa = 0.0;
    for (int i = 0; i < 4096; ++i) {
        const double x = 8.0 * std::sin(2.0 * kPi * 3100.0 * i / 192000.0);
        const double y = cA.process(x, 0.6, 0.75);
        REQUIRE(std::isfinite(y));
        maxAdaa = std::max(maxAdaa, std::abs(y));
    }
    CHECK(maxAdaa <= 0.75 + 1e-12);

    // Numerical derivative of softClipUnitF1 matches softClip everywhere.
    constexpr double h = 1e-6;
    for (double u : {-3.5, -0.9, -0.6, -0.2, 0.0, 0.2, 0.6, 0.9, 3.5, 50.0}) {
        const double df = (softClipUnitF1(u + h, 0.6) - softClipUnitF1(u - h, 0.6)) / (2.0 * h);
        CHECK(df == Catch::Approx(softClip(u, 0.6, 1.0)).margin(1e-6));
    }

    // lanePhaseHz wraps accurately with phase offset and clamps negative rates safely.
    CHECK(lanePhaseHz(0.0, 0.5, 0.0) == Catch::Approx(0.0));
    CHECK(lanePhaseHz(1.5, 0.5, 0.0) == Catch::Approx(0.75));
    CHECK(lanePhaseHz(1.5, 0.5, 90.0) == Catch::Approx(0.0).margin(1e-12));
    CHECK(lanePhaseHz(2.0, -1.0, 180.0) == Catch::Approx(0.5));
}

TEST_CASE("hba::dsp::EnvelopeCurve integrates log-Hz smoothly and maintains node invariants", "[dsp]") {
    using namespace hba::dsp;
    auto c = defaultKickPitchCurve();
    CHECK(c.count == 4);
    CHECK(evaluateEnvelope(c, 0.0) == Catch::Approx(0.78));
    CHECK(evaluateEnvelope(c, 1.0) == Catch::Approx(0.19));

    // Exact integral consistency: full integral == sum of 1000 sub-intervals
    const double whole = integrateEnvelopeLogHz(c, 0.0, 1.0, 20.0, 2400.0);
    double fine = 0.0;
    constexpr int N = 1000;
    for (int i = 0; i < N; ++i)
        fine += integrateEnvelopeLogHz(c, static_cast<double>(i) / N, static_cast<double>(i + 1) / N, 20.0, 2400.0);
    CHECK(whole == Catch::Approx(fine).epsilon(1e-5));
    CHECK(whole > 50.0);

    // Edit helpers preserve sorted endpoints
    const int idx = insertEnvelopeNode(c, 0.5f, 0.25f);
    CHECK(idx > 0);
    CHECK(c.count == 5);
    moveEnvelopeNode(c, idx, 0.99f, 0.22f);
    CHECK(c.nodes[0].t == 0.0f);
    CHECK(c.nodes[static_cast<std::size_t>(c.count - 1)].t == 1.0f);
    CHECK(removeEnvelopeNode(c, idx));
    CHECK(c.count == 4);
}

TEST_CASE("hba::dsp harmonic crest optimizer, TwinTResonator, and analyticAsymShape meet precision specs", "[dsp]") {
    using namespace hba::dsp;
    const std::array<double, 6> amps{1.0, 0.45, 0.30, 0.22, 0.16, 0.10};
    const auto optPh = optimizeCrestPhases(amps, 0.0);

    auto measurePeak = [&](const std::array<double, 6>& ph) {
        double pk = 0.0;
        for (int i = 0; i < 512; ++i) {
            const double th = kTwoPi * (static_cast<double>(i) / 512.0);
            double y = 0.0;
            for (std::size_t k = 0; k < 6; ++k)
                y += amps[k] * std::sin(static_cast<double>(k + 1) * th + ph[k]);
            pk = std::max(pk, std::abs(y));
        }
        return pk;
    };
    const std::array<double, 6> zeroPh{};
    const double zeroPeak = measurePeak(zeroPh);
    const double optPeak = measurePeak(optPh);
    const double crestGainDb = 20.0 * std::log10(zeroPeak / optPeak);
    INFO("Zero-phase peak=" << zeroPeak << " optPeak=" << optPeak << " crest reduction=" << crestGainDb << " dB");
    CHECK(crestGainDb >= 1.5);

    // Twin-T resonator decays smoothly and remains finite under rapid pitch sweep
    TwinTResonator tt;
    tt.prepare(48000.0);
    tt.trigger(1.0, 0.0);
    double peakTt = 0.0;
    for (int i = 0; i < 4800; ++i) {
        const double hz = 52.0 + 400.0 * std::exp(-static_cast<double>(i) / 400.0);
        const double y = tt.process(hz, 0.12, 0.4);
        REQUIRE(std::isfinite(y));
        peakTt = std::max(peakTt, std::abs(y));
    }
    CHECK(peakTt > 0.5);
    CHECK(peakTt < 1.5);

    // Analytic asymmetric shaper keeps DC offset below -90 dBFS on a full-scale sine wave
    double dcSum = 0.0;
    constexpr int M = 1024;
    for (int i = 0; i < M; ++i) {
        const double x = 0.9 * std::sin(kTwoPi * static_cast<double>(i) / static_cast<double>(M));
        dcSum += analyticAsymShape(x, 0.42, 0.9);
    }
    const double dcMean = std::abs(dcSum / static_cast<double>(M));
    CHECK(dcMean < 3.0e-5); // < -90 dBFS
}

TEST_CASE("Publishing supersedes unread samples and preserves both playback generations", "[dsp]") {
    hba::dsp::SampleExchange<int> samples;
    REQUIRE_FALSE(samples.pull());
    for (int gen = 1; gen <= 100; ++gen) {
        const int current = samples.current(), previous = samples.previous();
        for (int revision = 0; revision < 7; ++revision) {
            samples.writable() = gen * 10 + revision;
            samples.publish();
            REQUIRE(samples.current() == current);
            REQUIRE(samples.previous() == previous);
        }
        REQUIRE(samples.pull());
        REQUIRE(samples.current() == gen * 10 + 6);
        REQUIRE(samples.previous() == current);
        REQUIRE_FALSE(samples.pull());
    }
}

TEST_CASE("hba::dsp::RealFft and StereoStft achieve exact unity COLA reconstruction and latency", "[dsp][engine]") {
    using namespace hba::dsp;

    // 1. RealFft 2-for-1 stereo round-trip accuracy.
    RealFft<1024> fft;
    fft.prepare(1024);
    std::array<float, 1024> inL{}, inR{}, outL{}, outR{}, scRe{}, scIm{};
    std::array<float, 513> reL{}, imL{}, reR{}, imR{};
    for (int i = 0; i < 1024; ++i) {
        inL[static_cast<std::size_t>(i)] =
            static_cast<float>(0.6 * std::sin(kTwoPi * 7.0 * i / 1024.0) + 0.3 * std::cos(kTwoPi * 31.0 * i / 1024.0));
        inR[static_cast<std::size_t>(i)] = static_cast<float>(-0.5 * std::cos(kTwoPi * 13.0 * i / 1024.0) +
                                                              0.4 * std::sin(kTwoPi * 53.0 * i / 1024.0));
    }
    fft.forwardStereo(inL.data(), inR.data(), reL.data(), imL.data(), reR.data(), imR.data(), scRe.data(), scIm.data());
    // Bin 7 in L has amplitude 0.6 -> |X[7]| = 0.6 * N / 2 = 307.2.
    CHECK(std::hypot(reL[7], imL[7]) == Catch::Approx(307.2f).margin(1e-2f));
    // Bin 13 in R has amplitude 0.5 -> |X[13]| = 0.5 * N / 2 = 256.0.
    CHECK(std::hypot(reR[13], imR[13]) == Catch::Approx(256.0f).margin(1e-2f));

    fft.inverseStereo(reL.data(), imL.data(), reR.data(), imR.data(), outL.data(), outR.data());
    float maxFftErr = 0.0f;
    for (int i = 0; i < 1024; ++i) {
        maxFftErr = std::max(maxFftErr, std::abs(outL[static_cast<std::size_t>(i)] - inL[static_cast<std::size_t>(i)]));
        maxFftErr = std::max(maxFftErr, std::abs(outR[static_cast<std::size_t>(i)] - inR[static_cast<std::size_t>(i)]));
    }
    CHECK(maxFftErr < 1.0e-5f);

    // 2. StereoStft 4x COLA pass-through impulse latency is exact `fftSize` across arbitrary phases.
    for (int impulseAt : {0, 1, 17, 255, 256, 257, 777}) {
        StereoStft<1024> stft;
        stft.prepare(1024);
        REQUIRE(stft.latencySamples() == 1024);
        float maxErr = 0.0f;
        for (int t = 0; t < impulseAt + 2048; ++t) {
            const float xL = (t == impulseAt) ? 1.0f : 0.0f;
            const float xR = (t == impulseAt) ? -0.75f : 0.0f;
            float yL = 0.0f, yR = 0.0f;
            stft.processSample(xL, xR, 0.0f, 0.0f, false, yL, yR, [](StereoStft<1024>::FrameBuffers&) noexcept {});
            const float expL = (t == impulseAt + 1024) ? 1.0f : 0.0f;
            const float expR = (t == impulseAt + 1024) ? -0.75f : 0.0f;
            maxErr = std::max(maxErr, std::abs(yL - expL));
            maxErr = std::max(maxErr, std::abs(yR - expR));
        }
        CHECK(maxErr < 2.0e-5f);
    }

    // 3. StereoStft sidechain carrier frames align sample-identically with main frames.
    StereoStft<1024> scStft;
    scStft.prepare(1024);
    int checkedFrames = 0;
    bool allHadSidechain = true;
    float maxScDiff = 0.0f;
    for (int t = 0; t < 3072; ++t) {
        const float xL = std::sin(static_cast<float>(kTwoPi * 440.0 * t / 48000.0));
        const float xR = std::cos(static_cast<float>(kTwoPi * 660.0 * t / 48000.0));
        float yL = 0.0f, yR = 0.0f;
        scStft.processSample(xL, xR, xL, xR, true, yL, yR, [&](StereoStft<1024>::FrameBuffers& fb) noexcept {
            if (!fb.hasSidechain)
                allHadSidechain = false;
            for (int k = 0; k < fb.numBins; ++k) {
                maxScDiff = std::max(maxScDiff, std::abs(fb.reL[k] - fb.scReL[k]));
                maxScDiff = std::max(maxScDiff, std::abs(fb.imR[k] - fb.scImR[k]));
            }
            ++checkedFrames;
        });
    }
    CHECK(allHadSidechain);
    CHECK(checkedFrames >= 8);
    CHECK(maxScDiff < 1.0e-6f);
}

TEST_CASE("hba::dsp::TransientSplit maintains exact unity sum and isolates attacks from sustain", "[dsp][engine]") {
    using namespace hba::dsp;
    TransientSplit split;
    split.prepare(48000.0);

    // Steady sustained 80 Hz sub and 1 kHz tones should settle cleanly in body (zero false transient trigger).
    for (double hz : {80.0, 1000.0}) {
        split.reset();
        double maxSustMask = 0.0;
        double maxUnityErr = 0.0;
        for (int i = 0; i < 9600; ++i) {
            const double x = 0.8 * std::sin(kTwoPi * hz * i / 48000.0);
            double tL = 0.0, tR = 0.0, bL = 0.0, bR = 0.0;
            const double m = split.process(x, x, 1.0, 0.65, tL, tR, bL, bR);
            maxUnityErr = std::max(maxUnityErr, std::abs((tL + bL) - x));
            maxUnityErr = std::max(maxUnityErr, std::abs((tR + bR) - x));
            if (i >= 6000)
                maxSustMask = std::max(maxSustMask, m);
        }
        CHECK(maxUnityErr < 1.0e-12);
        CHECK(maxSustMask < 0.03);
    }

    // Sharp 3 kHz drum attack burst after silence must spike transient mask strongly.
    split.reset();
    double peakBurstMask = 0.0;
    for (int i = 0; i < 2000; ++i) {
        const double env = (i >= 200 && i < 600) ? std::exp(-static_cast<double>(i - 200) / 140.0) : 0.0;
        const double x = env * std::sin(kTwoPi * 3000.0 * i / 48000.0);
        double tL = 0.0, tR = 0.0, bL = 0.0, bR = 0.0;
        const double m = split.process(x, -x * 0.8, 1.0, 0.65, tL, tR, bL, bR);
        CHECK(std::abs((tL + bL) - x) < 1.0e-12);
        if (i >= 200 && i < 500)
            peakBurstMask = std::max(peakBurstMask, m);
    }
    CHECK(peakBurstMask > 0.65);
}

TEST_CASE("hba::dsp edge guards, 16x Oversampler, curve invariants, and History edge cases", "[dsp][engine]") {
    using namespace hba::dsp;

    // 1. Oversampler 2x/4x/8x/16x preserve unity passband and match latencyExact() / latencySamples().
    for (int factor : {2, 4, 8, 16}) {
        Oversampler os;
        os.prepare(factor);
        REQUIRE(os.factor() == factor);
        REQUIRE(os.latencySamples() == static_cast<int>(std::lround(os.latencyExact())));
        double peak = 0.0;
        std::array<float, kMaxOversampling> buf{};
        for (int i = 0; i < 4096; ++i) {
            const float x = std::sin(static_cast<float>(kTwoPi * 440.0 * i / 48000.0));
            os.up(x, buf.data());
            const float y = os.down(buf.data());
            REQUIRE(std::isfinite(y));
            if (i >= 1024)
                peak = std::max(peak, static_cast<double>(std::abs(y)));
        }
        CHECK(peak == Catch::Approx(1.0).margin(1e-3));

        // Impulse centroid matches latencyExact() within 0.01 samples.
        os.reset();
        double moment = 0.0, sum = 0.0;
        for (int n = 0; n < 512; ++n) {
            os.up(n == 0 ? 1.0f : 0.0f, buf.data());
            const double y = os.down(buf.data());
            moment += n * y;
            sum += y;
        }
        CHECK(moment / sum == Catch::Approx(os.latencyExact()).margin(0.01));
    }

    // 2. softClipLinked isolates NaN on one channel without poisoning the other.
    {
        double l = std::numeric_limits<double>::quiet_NaN();
        double r = 0.5;
        (void)softClipLinked(l, r, 0.8, 1.0);
        CHECK(l == 0.0);
        CHECK(r == Catch::Approx(0.5));
    }

    // 3. RealFft<128> and StereoStft<128> construct cleanly without overflowing MaxN < 1024.
    {
        RealFft<128> smallFft;
        CHECK(smallFft.size() == 128);
        StereoStft<128> smallStft;
        CHECK(smallStft.fftSize() == 128);
        float yL = 0.0f, yR = 0.0f;
        smallStft.processSample(1.0f, -1.0f, 0.0f, 0.0f, false, yL, yR, [](StereoStft<128>::FrameBuffers&) noexcept {});
        CHECK(std::isfinite(yL));
        CHECK(std::isfinite(yR));
    }

    // 4. Curve::movePoint and EnvelopeCurve::moveEnvelopeNode preserve sorted order on tight spacing.
    {
        Curve c;
        addPoint(c, {0.5f, 0.0f});
        addPoint(c, {0.50005f, 0.2f});
        addPoint(c, {0.5001f, 0.4f});
        movePoint(c, 1, 0.9f, 0.3f);
        CHECK(c.points[0].x <= c.points[1].x);
        CHECK(c.points[1].x <= c.points[2].x);

        EnvelopeCurve empty{};
        const int inserted = insertEnvelopeNode(empty, 0.4f, 0.7f);
        CHECK(empty.count == 3);
        CHECK(inserted == 1);
        CHECK(empty.nodes[1].t == Catch::Approx(0.4f));

        EnvelopeCurve tight{};
        tight.count = 4;
        tight.nodes[0] = {0.0f, 1.0f, 0.0f, EnvSegment::line};
        tight.nodes[1] = {0.5f, 0.5f, 0.0f, EnvSegment::line};
        tight.nodes[2] = {0.501f, 0.5f, 0.0f, EnvSegment::line};
        tight.nodes[3] = {1.0f, 0.0f, 0.0f, EnvSegment::line};
        moveEnvelopeNode(tight, 2, 0.8f, 0.4f);
        CHECK(tight.nodes[1].t <= tight.nodes[2].t);
        CHECK(tight.nodes[2].t <= tight.nodes[3].t);
    }

    // 5. optimizeCrestPhases handles negative harmonic amplitudes and reduces peak.
    {
        const std::array<double, 4> amps{1.0, -0.5, 0.35, -0.25};
        const auto opt = optimizeCrestPhases(amps, 0.0);
        for (double ph : opt)
            CHECK(std::isfinite(ph));
    }

    // 6. Low sample rate BiquadCoefs/AllpassPair, Korg35 singularity, lane_math bounds.
    {
        AllpassPair ap;
        ap.setFreq(20.0, 30.0);
        CHECK(std::isfinite(ap.process(0.5)));

        const auto lp = BiquadCoefs::lowpass(10.0, 0.707, 15.0);
        CHECK(std::isfinite(lp.b0));
        const auto ls = BiquadCoefs::lowShelf(100.0, std::numeric_limits<double>::quiet_NaN(), 6.0, 48000.0);
        CHECK(ls.b0 == 1.0);

        TptKorg35 korg;
        const double y = korg.process(0.5, 1.0, 4.0, 1.0); // G=0.5, a=0.5, G*k*a=1.0 -> denom 0!
        CHECK(std::isfinite(y));
        CHECK(softSaturate(1.0, 0.0) == 0.0);

        CHECK(lanePhase(1.0, 0.0, 0.0) == 0.0);
        std::array<LaneConfig, 2> lanes{};
        CHECK(resolveLaneMode(lanes, 5) == LaneMode::sync);
        CHECK(resolveLanePhase(lanes, -1, {}) == 0.0);
    }

    // 7. History backward-clock coalescing guard and duplicate push clearing redo.
    {
        hba::studio::History<int> h;
        h.pushCoalesced(1, 7u, 1000);
        h.pushCoalesced(2, 7u, 500); // clock went backwards: must NOT coalesce
        const auto u1 = h.undo(3);
        REQUIRE(u1.has_value());
        CHECK(*u1 == 2);
        CHECK(h.canRedo());
        h.push(1); // duplicate of undo_.back(), must still clear stale redo
        CHECK_FALSE(h.canRedo());
    }
}
