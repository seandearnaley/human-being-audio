// SPDX-License-Identifier: GPL-3.0-or-later
#include <hba/dsp/control.h>
#include <hba/dsp/svf.h>
#include <hba/dsp/unison.h>
#include <hba/dsp/va_filter.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <set>

namespace {

constexpr double kRate = 192000.0;

template <typename Filter> double settleDc(Filter& f, double g, double k, double level) {
    double y = 0.0;
    for (int n = 0; n < 20000; ++n)
        y = f.process(level, g, k, 1.0);
    return y / level;
}

// Rings the filter with an impulse, then measures the steady oscillation's peak
// and frequency over the last half second.
template <typename Filter> std::pair<double, double> ring(Filter& f, double g, double k) {
    const int total = static_cast<int>(kRate);
    double peak = 0.0, prev = 0.0;
    int crossings = 0;
    bool finite = true;
    for (int n = 0; n < total; ++n) {
        const double y = f.process(n == 0 ? 0.5 : 0.0, g, k, 1.0);
        finite = finite && std::isfinite(y);
        if (n >= total / 2) {
            peak = std::max(peak, std::abs(y));
            if (prev < 0.0 && y >= 0.0)
                ++crossings;
        }
        prev = y;
    }
    REQUIRE(finite);
    return {peak, static_cast<double>(crossings) / 0.5};
}

} // namespace

TEST_CASE("unison: a stack of one is the bare voice and wider stacks keep its power", "[dsp][unison]") {
    const auto one = hba::dsp::unisonGains(1, 0.8);
    CHECK(one.centre == 1.0);
    CHECK(one.side == 0.0);
    for (int n = 2; n <= hba::dsp::kMaxUnison; ++n) {
        const auto g = hba::dsp::unisonGains(n, 0.8);
        CHECK(g.centre * g.centre + static_cast<double>(n - 1) * g.side * g.side == Catch::Approx(1.0));
        CHECK(g.side == Catch::Approx(0.8 * g.centre));
    }
}

TEST_CASE("unison: start phases are deterministic, in range and distinct", "[dsp][unison]") {
    std::array<double, hba::dsp::kMaxUnison> a{}, b{}, c{};
    hba::dsp::unisonStartPhases(42u, a);
    hba::dsp::unisonStartPhases(42u, b);
    hba::dsp::unisonStartPhases(43u, c);
    CHECK(a == b);
    CHECK(a != c);
    CHECK(a[0] == 0.0);
    std::set<double> seen(a.begin(), a.end());
    CHECK(seen.size() == a.size());
    for (double p : a) {
        CHECK(p >= 0.0);
        CHECK(p < 1.0);
    }
}

TEST_CASE("unison: spread phases are fixed, in range and distinct", "[dsp][unison]") {
    std::array<double, hba::dsp::kMaxUnison> a{}, b{}, c{};
    hba::dsp::unisonSpreadPhases(0.0, a);
    hba::dsp::unisonSpreadPhases(0.0, b);
    hba::dsp::unisonSpreadPhases(0.5, c);
    CHECK(a == b);
    CHECK(a != c);
    CHECK(a[0] == 0.0);
    std::set<double> seen(a.begin(), a.end());
    CHECK(seen.size() == a.size());
    for (double p : c) {
        CHECK(p >= 0.0);
        CHECK(p < 1.0);
    }
}

TEST_CASE("unison: offset tables start at the centre and reach the widest voice", "[dsp][unison]") {
    for (const auto& table : {hba::dsp::kSzaboOffsets, hba::dsp::kSymmetricOffsets}) {
        CHECK(table[0] == 0.0);
        double widest = 0.0;
        for (double o : table)
            widest = std::max(widest, std::abs(o));
        CHECK(widest == Catch::Approx(1.0));
    }
}

TEST_CASE("va filters: unity gain at DC without resonance", "[dsp][vafilter]") {
    const double g = hba::dsp::SvfCoefs::prewarp(1000.0, kRate);
    hba::dsp::TptLadder ladder;
    CHECK(settleDc(ladder, g, 0.0, 0.01) == Catch::Approx(1.0).margin(1e-3));
    hba::dsp::TptKorg35 korg;
    CHECK(settleDc(korg, g, 0.0, 0.01) == Catch::Approx(1.0).margin(1e-3));
    // With feedback the ladder's DC gain falls to 1 / (1 + k), the Moog way.
    hba::dsp::TptLadder resonant;
    CHECK(settleDc(resonant, g, 2.0, 0.01) == Catch::Approx(1.0 / 3.0).margin(1e-3));
}

TEST_CASE("va filters: self-oscillation is bounded and sits at the cutoff", "[dsp][vafilter]") {
    const double cutoff = 440.0;
    const double g = hba::dsp::SvfCoefs::prewarp(cutoff, kRate);
    {
        hba::dsp::TptLadder f;
        const auto [peak, hz] = ring(f, g, 1.05 * hba::dsp::TptLadder::kSelfOscillation);
        CHECK(peak > 0.05);
        CHECK(peak < 2.0);
        CHECK(hz == Catch::Approx(cutoff).epsilon(0.05));
    }
    {
        hba::dsp::TptKorg35 f;
        const auto [peak, hz] = ring(f, g, 1.05 * hba::dsp::TptKorg35::kSelfOscillation);
        CHECK(peak > 0.05);
        CHECK(peak < 2.0);
        CHECK(hz == Catch::Approx(cutoff).epsilon(0.05));
    }
}

TEST_CASE("va filters: below self-oscillation an impulse dies away", "[dsp][vafilter]") {
    const double g = hba::dsp::SvfCoefs::prewarp(2000.0, kRate);
    hba::dsp::TptLadder ladder;
    hba::dsp::TptKorg35 korg;
    const auto [lp, lhz] = ring(ladder, g, 3.0);
    const auto [kp, khz] = ring(korg, g, 1.5);
    CHECK(lp < 1e-6);
    CHECK(kp < 1e-6);
}
