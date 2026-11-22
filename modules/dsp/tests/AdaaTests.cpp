// SPDX-License-Identifier: GPL-3.0-or-later
#include <hba/dsp/adaa.h>

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

// Measures inharmonic aliasing power relative to harmonic signal power (dB)
// for a sine wave whose fundamental is an exact bin of an N-point DFT.
// Since the input is odd-symmetric around 0 and all shapers are odd, true
// harmonics lie on odd multiples of k0; all other bins (excluding DC and
// exact harmonic folds that coincide with harmonics) measure aliasing energy.
// Choosing k0 coprime to N/2 keeps aliased harmonics off the fundamental/low
// odd harmonic bins so the measurement cleanly separates aliasing from tone.
template <typename Fn>
double measureAliasingRatioDb(Fn&& fn, double drive) {
    constexpr int kN = 2048;
    constexpr int kFundBin = 171;  // ~4008 Hz at 48 kHz, odd & coprime to 2048
    constexpr int kWarmup = 64;

    for (int i = -kWarmup; i < 0; ++i) {
        const double x = drive * std::sin(2.0 * kPi * kFundBin * i / kN);
        (void)fn(x);
    }

    std::vector<double> y(kN, 0.0);
    for (int i = 0; i < kN; ++i) {
        const double x = drive * std::sin(2.0 * kPi * kFundBin * i / kN);
        y[static_cast<std::size_t>(i)] = fn(x);
    }

    // Identify true unaliased odd harmonic bins below Nyquist (f < fs/2 => bin < kN/2).
    std::vector<bool> isHarmonic(kN / 2 + 1, false);
    for (int h = 1; h * kFundBin < kN / 2; h += 2) isHarmonic[static_cast<std::size_t>(h * kFundBin)] = true;

    double harmPower = 0.0;
    double aliasPower = 0.0;
    for (int k = 1; k < kN / 2; ++k) {
        double re = 0.0, im = 0.0;
        const double w = 2.0 * kPi * k / kN;
        for (int n = 0; n < kN; ++n) {
            const double ang = w * n;
            re += y[static_cast<std::size_t>(n)] * std::cos(ang);
            im -= y[static_cast<std::size_t>(n)] * std::sin(ang);
        }
        const double p = re * re + im * im;
        if (isHarmonic[static_cast<std::size_t>(k)]) harmPower += p;
        else aliasPower += p;
    }
    return 10.0 * std::log10((aliasPower + 1e-30) / (harmPower + 1e-30));
}

}  // namespace

TEMPLATE_TEST_CASE("ADAA2 shapers satisfy exact antiderivative identities", "[dsp][adaa]",
                   hba::dsp::TanhShaper, hba::dsp::HardClipShaper, hba::dsp::TriangleFoldShaper,
                   hba::dsp::Buchla259Shaper, hba::dsp::SineFoldShaper) {
    using S = TestType;

    // Odd symmetry of f and F2, even symmetry of F1, bounded output.
    for (double u : { 0.0, 1e-6, 0.25, 0.5, 0.75, 1.5, 3.7, 8.2, 16.0, 40.0 }) {
        CHECK_THAT(S::eval(-u), Catch::Matchers::WithinAbs(-S::eval(u), 1e-13));
        CHECK_THAT(S::ad1(-u), Catch::Matchers::WithinAbs(S::ad1(u), 1e-13));
        CHECK_THAT(S::ad2(-u), Catch::Matchers::WithinAbs(-S::ad2(u), 1e-12));
        CHECK(std::abs(S::eval(u)) <= 1.2);
    }

    // Unity or near-unity small-signal linear region around 0.
    CHECK(S::eval(0.0) == 0.0);

    // Central finite-difference check of F1' == f and F2' == F1 away from C^0 corners.
    const double h = 1e-6;
    for (double u : { -18.5, -7.25, -2.3, -0.73, -0.5, -0.18, 0.0, 0.18, 0.5, 0.73, 2.3, 7.25, 18.5 }) {
        const double d1 = (S::ad1(u + h) - S::ad1(u - h)) / (2.0 * h);
        const double d2 = (S::ad2(u + h) - S::ad2(u - h)) / (2.0 * h);
        CHECK_THAT(d1, Catch::Matchers::WithinAbs(S::eval(u), 1e-5));
        CHECK_THAT(d2, Catch::Matchers::WithinAbs(S::ad1(u), 1e-4));
    }
}

TEMPLATE_TEST_CASE("ADAA2 handles turning points, plateaus, and extreme sweeps cleanly", "[dsp][adaa]",
                   hba::dsp::TanhShaper, hba::dsp::HardClipShaper, hba::dsp::TriangleFoldShaper,
                   hba::dsp::Buchla259Shaper, hba::dsp::SineFoldShaper) {
    using S = TestType;
    hba::dsp::Adaa2<S> proc;

    // Startup from reset on silence produces exact zero (no F1(0)/F2(0) spike).
    for (int i = 0; i < 8; ++i) CHECK_THAT(proc.process(0.0), Catch::Matchers::WithinAbs(0.0, 1e-12));

    // Exact turning point: x2 = a, x1 = b, x0 = a + eps across threshold boundary.
    // Must be continuous as eps crosses kEps2 without jumping to f(a).
    const double a = 0.2;
    const double b = 3.4;
    auto evalAt = [&](double eps) {
        hba::dsp::Adaa2<S> p;
        p.prime(b, a);
        return p.process(a + eps);
    };
    const double yExact = evalAt(0.0);
    const double yAbove = evalAt(2.0e-4);
    const double yBelow = evalAt(0.5e-4);
    CHECK(std::isfinite(yExact));
    CHECK_THAT(yBelow, Catch::Matchers::WithinAbs(yExact, 1e-4));
    CHECK_THAT(yAbove, Catch::Matchers::WithinAbs(yExact, 1e-3));

    // Constant plateau converges to exact static value.
    proc.prime(1.75, 1.75);
    CHECK_THAT(proc.process(1.75), Catch::Matchers::WithinAbs(S::eval(1.75), 1e-12));
}

TEMPLATE_TEST_CASE("ADAA2 materially beats naive shaping and ADAA1 on high-drive aliasing", "[dsp][adaa]",
                   hba::dsp::TanhShaper, hba::dsp::HardClipShaper, hba::dsp::TriangleFoldShaper,
                   hba::dsp::Buchla259Shaper, hba::dsp::SineFoldShaper) {
    using S = TestType;
    const double drive = 6.0;

    const double naiveDb = measureAliasingRatioDb([](double x) { return S::eval(x); }, drive);

    hba::dsp::Adaa1<S> adaa1;
    const double adaa1Db = measureAliasingRatioDb([&](double x) { return adaa1.process(x); }, drive);

    hba::dsp::Adaa2<S> adaa2;
    const double adaa2Db = measureAliasingRatioDb([&](double x) { return adaa2.process(x); }, drive);

    INFO("naive=" << naiveDb << " dB, ADAA1=" << adaa1Db << " dB, ADAA2=" << adaa2Db << " dB");
    CHECK(adaa1Db < naiveDb - 5.0);
    CHECK(adaa2Db < adaa1Db - 4.0);
    CHECK(adaa2Db < naiveDb - 14.0);
}

TEST_CASE("softGuard is C^2 continuous across knee and strictly bounded", "[dsp][adaa]") {
    using hba::dsp::softGuard;

    CHECK(softGuard(0.0) == 0.0);
    CHECK(softGuard(2.5) == 2.5);
    CHECK(softGuard(-2.5) == -2.5);
    CHECK(std::abs(softGuard(1.0e6)) <= 4.0);
    CHECK(std::abs(softGuard(-1.0e6)) <= 4.0);

    // Value and derivative continuity across knee = 2.5.
    const double h = 1e-5;
    const double slopeBelow = (softGuard(2.5) - softGuard(2.5 - h)) / h;
    const double slopeAbove = (softGuard(2.5 + h) - softGuard(2.5)) / h;
    CHECK_THAT(slopeBelow, Catch::Matchers::WithinAbs(1.0, 1e-9));
    CHECK_THAT(slopeAbove, Catch::Matchers::WithinAbs(1.0, 1e-6));

    // Zero curvature at knee (C^2 smoothness).
    const double curvAbove = (softGuard(2.5 + 2.0 * h) - 2.0 * softGuard(2.5 + h) + softGuard(2.5)) / (h * h);
    CHECK(std::abs(curvAbove) < 1e-3);
}
