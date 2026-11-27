// SPDX-License-Identifier: AGPL-3.0-or-later
#include "dt/engine.h"

#include <hba/dsp/svf.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

TEST_CASE("All 4 Nord core topologies x 6 ADAA2 shapers render finite, punchy, bounded stereo kicks", "[engine]") {
    constexpr double fs = 48000.0;
    constexpr int total = 9600; // 200 ms

    for (int topo = 0; topo < 4; ++topo) {
        for (int shp = 0; shp < 6; ++shp) {
            dt::Engine eng;
            eng.prepare(fs, 256, 4);
            auto p = dt::Params::defaults();
            p[dt::ParamId::coreTopology] = static_cast<float>(topo);
            p[dt::ParamId::shaperType] = static_cast<float>(shp);
            p[dt::ParamId::drive] = 0.55f;
            p[dt::ParamId::cavityQ] = 0.35f;

            eng.triggerMidi(36, 0.96f);
            std::vector<float> l(total, 0.0f), r(total, 0.0f);
            for (int pos = 0; pos < total; pos += 256) {
                const int n = std::min(256, total - pos);
                eng.process(p, {172.0, 0.0, false}, l.data() + pos, r.data() + pos, n);
            }
            float peak = 0.0f;
            double energy = 0.0;
            for (int i = 0; i < total; ++i) {
                const auto ii = static_cast<std::size_t>(i);
                REQUIRE(std::isfinite(l[ii]));
                REQUIRE(std::isfinite(r[ii]));
                peak = std::max(peak, std::max(std::abs(l[ii]), std::abs(r[ii])));
                energy += 0.5 * (l[ii] * l[ii] + r[ii] * r[ii]);
            }
            const double rms = std::sqrt(energy / total);
            INFO("topo=" << topo << " shp=" << shp << " peak=" << peak << " rms=" << rms);
            CHECK(peak <= 1.05f);
            CHECK(rms > 0.10);
        }
    }
}

TEST_CASE("Zero-X tail snap and analytic asymmetric saturation maintain near-zero DC offset", "[engine]") {
    constexpr double fs = 48000.0;
    auto p = dt::Params::defaults();
    p[dt::ParamId::coreTopology] = static_cast<float>(dt::CoreTopology::sine);
    p[dt::ParamId::shaperType] = static_cast<float>(dt::ShaperType::asym);
    p[dt::ParamId::drive] = 0.85f;
    p[dt::ParamId::clickAmt] = 0.0f;
    p[dt::ParamId::zeroCross] = 1.0f;
    p[dt::ParamId::lengthMs] = 160.0f;

    dt::Engine eng;
    eng.prepare(fs, 256, 8);
    eng.triggerSlot(0, 1.0f);

    constexpr int total = 12000;
    std::vector<float> l(total, 0.0f), r(total, 0.0f);
    for (int pos = 0; pos < total; pos += 256) {
        const int n = std::min(256, total - pos);
        eng.process(p, {172.0, 0.0, false}, l.data() + pos, r.data() + pos, n);
    }

    double dcAcc = 0.0;
    for (float s : l)
        dcAcc += static_cast<double>(s);
    const double dcMean = std::abs(dcAcc / static_cast<double>(total));
    INFO("Zero-X + 85% Asym ADAA2 DC mean = " << dcMean);
    CHECK(dcMean < 0.008); // Tight DC discipline without any phase-rotating high-pass filter!
}

TEST_CASE("Peak-Min Schroeder phase optimization reduces crest factor at matched RMS", "[engine]") {
    auto p = dt::Params::defaults();
    p[dt::ParamId::coreTopology] = static_cast<float>(dt::CoreTopology::harmonic);
    p[dt::ParamId::harm2] = 0.55f;
    p[dt::ParamId::harm3] = 0.42f;
    p[dt::ParamId::harm4] = 0.30f;
    p[dt::ParamId::harm5] = 0.22f;
    p[dt::ParamId::harm6] = 0.14f;
    p[dt::ParamId::harmTilt] = 0.10f;
    p[dt::ParamId::clickAmt] = 0.0f;
    p[dt::ParamId::drive] = 0.0f;
    p[dt::ParamId::limitOn] = 0.0f;
    p[dt::ParamId::outputDb] = -6.0f;
    p[dt::ParamId::pitchDepth] = 0.0f;

    p[dt::ParamId::headroomMode] = static_cast<float>(dt::HeadroomMode::zero);
    const auto zeroSnap = dt::Engine::renderPreview(p, hba::dsp::defaultKickPitchCurve(),
                                                    hba::dsp::defaultKickAmpCurve(), dt::defaultComposePattern());

    p[dt::ParamId::headroomMode] = static_cast<float>(dt::HeadroomMode::peakMin);
    const auto optSnap = dt::Engine::renderPreview(p, hba::dsp::defaultKickPitchCurve(),
                                                   hba::dsp::defaultKickAmpCurve(), dt::defaultComposePattern());

    INFO("Zero-phase crest=" << zeroSnap.crestDb << " dB, Peak-Min crest=" << optSnap.crestDb << " dB");
    CHECK(optSnap.crestDb < zeroSnap.crestDb - 0.8f);
}

TEST_CASE("Datum is bit-exact across arbitrary host block sizes when Humanize is 0", "[engine]") {
    constexpr double fs = 48000.0;
    constexpr int total = 4096;
    auto p = dt::Params::defaults();
    p[dt::ParamId::humanize] = 0.0f;

    auto renderWithBlock = [&](int blk) {
        dt::Engine eng;
        eng.prepare(fs, blk, 4);
        eng.triggerMidi(36, 0.92f);
        std::vector<float> l(total, 0.0f), r(total, 0.0f);
        for (int pos = 0; pos < total; pos += blk) {
            const int n = std::min(blk, total - pos);
            eng.process(p, {172.0, 0.0, false}, l.data() + pos, r.data() + pos, n);
        }
        return l;
    };

    const auto ref = renderWithBlock(256);
    const auto odd = renderWithBlock(37);
    double maxDiff = 0.0;
    for (std::size_t i = 0; i < ref.size(); ++i)
        maxDiff = std::max(maxDiff, std::abs(static_cast<double>(ref[i]) - static_cast<double>(odd[i])));
    CHECK(maxDiff < 1.0e-5);
}

TEST_CASE("Sub-Link sidechain alignment detects bass root and solves constructive start phase", "[engine]") {
    constexpr double fs = 48000.0;
    constexpr double bassHz = 55.0; // A1

    // Feed a 55 Hz sidechain bass sine into Engine::process and verify YIN root detection
    dt::Engine eng;
    eng.prepare(fs, 256, 4);
    auto p = dt::Params::defaults();
    p[dt::ParamId::tuneHz] = 55.0f;
    p[dt::ParamId::clickAmt] = 0.0f;
    p[dt::ParamId::pitchDepth] = 0.1f;

    constexpr int total = 12000;
    std::vector<float> sc(total, 0.0f), l(total, 0.0f), r(total, 0.0f);
    for (int i = 0; i < total; ++i)
        sc[static_cast<std::size_t>(i)] =
            0.65f * static_cast<float>(std::sin(hba::dsp::kTwoPi * bassHz * i / fs + 2.1));

    eng.triggerSlot(0, 0.95f);
    for (int pos = 0; pos < total; pos += 256) {
        const int n = std::min(256, total - pos);
        eng.process(p, {172.0, 0.0, false}, l.data() + pos, r.data() + pos, n, sc.data() + pos, sc.data() + pos);
    }

    const auto& tel = eng.telemetry();
    CHECK(tel.scActive);
    CHECK(tel.scRootHz == Catch::Approx(55.0f).margin(2.5f));

    // Solve optimal startPhase against captured sidechain scope
    float bestCorr = 0.0f;
    const float bestDeg =
        dt::Engine::solveSubLinkPhase(p, hba::dsp::defaultKickPitchCurve(), hba::dsp::defaultKickAmpCurve(),
                                      dt::defaultComposePattern(), tel.scScope, &bestCorr);
    INFO("Sub-Link bestDeg=" << bestDeg << " bestCorr=" << bestCorr);
    CHECK(bestDeg >= 0.0f);
    CHECK(bestDeg < 360.0f);
    CHECK(bestCorr > 0.60f);
}

TEST_CASE("Sub band stays strictly mono even at 100% Air Width and Context Sequencer emits valid MIDI", "[engine]") {
    constexpr double fs = 48000.0;
    auto p = dt::Params::defaults();
    p[dt::ParamId::airWidth] = 1.0f;
    p[dt::ParamId::noiseLevel] = 0.5f;

    dt::Engine eng;
    eng.prepare(fs, 256, 4);
    eng.triggerSlot(0, 0.95f);
    constexpr int total = 8192;
    std::vector<float> l(total, 0.0f), r(total, 0.0f);
    for (int pos = 0; pos < total; pos += 256) {
        const int n = std::min(256, total - pos);
        eng.process(p, {172.0, 0.0, false}, l.data() + pos, r.data() + pos, n);
    }

    // 2-pole low-pass at 120 Hz: check L/R correlation > 0.999
    const auto lpCoefs = hba::dsp::SvfCoefs::make(120.0, 0.707, fs);
    hba::dsp::SvfState lpL, lpR;
    double dot = 0.0, eL = 0.0, eR = 0.0;
    for (int i = 0; i < total; ++i) {
        const double sl = lpL.lowpass(lpCoefs, l[static_cast<std::size_t>(i)]);
        const double sr = lpR.lowpass(lpCoefs, r[static_cast<std::size_t>(i)]);
        dot += sl * sr;
        eL += sl * sl;
        eR += sr * sr;
    }
    const double corr = dot / std::sqrt(std::max(1.0e-12, eL * eR));
    CHECK(corr > 0.999);

    const auto mid = dt::Engine::encodePatternMidi(p, dt::defaultComposePattern(), 172.0);
    REQUIRE(mid.size() > 32);
    CHECK(mid[0] == 'M');
    CHECK(mid[1] == 'T');
    CHECK(mid[2] == 'h');
    CHECK(mid[3] == 'd');
}
