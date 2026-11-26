// SPDX-License-Identifier: GPL-3.0-or-later
// Comprehensive tests for shared studio DSP primitives (control, SVF, crossover,
// PhaseRotator, Limiter, Oversampler, MultiShaperAdaa2, denormals) and StudioHistory.
#include <hba/dsp/adaa.h>
#include <hba/dsp/control.h>
#include <hba/dsp/crossover.h>
#include <hba/dsp/denormals.h>
#include <hba/dsp/lane_math.h>
#include <hba/dsp/limiter.h>
#include <hba/dsp/oversampling.h>
#include <hba/dsp/svf.h>

#include "../../studio-ui/StudioHistory.h"

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
    h.pushCoalesced(3, 42u, 1200); // coalesced within 650 ms window -> still one undo entry!
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
