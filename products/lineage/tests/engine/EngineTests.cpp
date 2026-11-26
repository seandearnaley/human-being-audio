// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ln/engine.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

TEST_CASE("All 6 core modes x 5 filter modes render finite, punchy, bounded stereo audio", "[engine]") {
    constexpr double fs = 48000.0;
    constexpr int total = 4800;

    for (int core = 0; core < 6; ++core) {
        for (int f1 = 0; f1 < 5; ++f1) {
            ln::Engine eng;
            eng.prepare(fs, 256, 4);
            auto p = ln::Params::defaults();
            p[ln::ParamId::coreMode] = static_cast<float>(core);
            p[ln::ParamId::filt1Mode] = static_cast<float>(f1);
            p[ln::ParamId::drive] = 0.55f;
            p[ln::ParamId::density] = 0.50f;
            p[ln::ParamId::disperse] = 0.40f;

            eng.noteOn(29, 0.95f); // F1
            std::vector<float> l(total, 0.0f), r(total, 0.0f);
            for (int pos = 0; pos < total; pos += 256) {
                const int n = std::min(256, total - pos);
                eng.process(p, {172.0, static_cast<double>(pos) * (172.0 / (60.0 * fs)), true}, l.data() + pos,
                            r.data() + pos, n);
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
            INFO("core=" << core << " f1=" << f1 << " peak=" << peak << " rms=" << rms);
            CHECK(peak <= 1.05f);
            CHECK(rms > 0.08);
        }
    }
}

TEST_CASE("Gen-2 live non-destructive print & stack generation produce clean key-tracked audio", "[engine]") {
    constexpr double fs = 48000.0;
    auto p = ln::Params::defaults();
    p[ln::ParamId::coreMode] = static_cast<float>(ln::CoreMode::analogReese);
    p[ln::ParamId::drive] = 0.50f;
    p[ln::ParamId::printBars] = 0.0f; // 1 bar

    const auto gen2 = ln::Engine::renderPrint(p, ln::defaultLaneCurves(), fs, 172.0, 29);
    REQUIRE(gen2.valid());
    CHECK(gen2.generation == 2);

    // Stack Gen 2 -> Gen 3
    p[ln::ParamId::filt2Mode] = static_cast<float>(ln::Filt2Mode::phaser);
    const auto gen3 = ln::Engine::renderPrint(p, ln::defaultLaneCurves(), fs, 172.0, 29, &gen2, true);
    REQUIRE(gen3.valid());
    CHECK(gen3.generation == 3);

    ln::Engine eng;
    eng.prepare(fs, 256, 4);
    eng.publishPrint(gen3);
    p[ln::ParamId::genMix] = 1.0f;
    p[ln::ParamId::strataDir] = static_cast<float>(ln::StrataDir::pingPong);
    eng.noteOn(36, 0.95f); // C2 (+7 st above F1 print root!)

    std::vector<float> l(4800), r(4800);
    for (int pos = 0; pos < 4800; pos += 256) {
        const int n = std::min(256, 4800 - pos);
        eng.process(p, {172.0, 0.0, true}, l.data() + pos, r.data() + pos, n);
    }
    double energy = 0.0;
    for (std::size_t i = 0; i < l.size(); ++i) {
        REQUIRE(std::isfinite(l[i]));
        REQUIRE(std::isfinite(r[i]));
        energy += 0.5 * (l[i] * l[i] + r[i] * r[i]);
    }
    CHECK(std::sqrt(energy / static_cast<double>(l.size())) > 0.08);
    CHECK(eng.telemetry().hasPrint);
    CHECK(eng.telemetry().generation == 3);
}

TEST_CASE("Lineage is bit-exact across block sizes and BPM-independent in Free Hz mode", "[engine]") {
    constexpr double fs = 48000.0;
    auto p = ln::Params::defaults();
    p[ln::ParamId::lane1Mode] = static_cast<float>(ln::LaneMode::freeRun);
    p[ln::ParamId::lane1Hz] = 1.75f;
    p[ln::ParamId::lane2Follow] = 1.0f;
    p[ln::ParamId::lane2Ratio] = 4.0f; // x2

    // 1) Block-size invariance
    auto renderWith = [&](int chunkSize) {
        ln::Engine eng;
        eng.prepare(fs, 512, 4);
        eng.noteOn(29, 0.9f);
        std::vector<float> l(1024), r(1024);
        for (int pos = 0; pos < 1024; pos += chunkSize) {
            const int n = std::min(chunkSize, 1024 - pos);
            eng.process(p, {172.0, static_cast<double>(pos) * (172.0 / (60.0 * fs)), true}, l.data() + pos,
                        r.data() + pos, n);
        }
        return std::pair{l, r};
    };
    const auto [refL, refR] = renderWith(512);
    for (int bs : {1, 7, 64, 257}) {
        const auto [curL, curR] = renderWith(bs);
        for (std::size_t i = 0; i < refL.size(); ++i) {
            REQUIRE(curL[i] == refL[i]);
            REQUIRE(curR[i] == refR[i]);
        }
    }

    // 2) BPM independence when all active lanes are Free / following Free
    ln::Engine engA, engB;
    engA.prepare(fs, 256, 4);
    engB.prepare(fs, 256, 4);
    engA.noteOn(29, 0.9f);
    engB.noteOn(29, 0.9f);
    std::vector<float> aL(2048), aR(2048), bL(2048), bR(2048);
    engA.process(p, {88.0, 0.5, true}, aL.data(), aR.data(), 2048);
    engB.process(p, {176.0, 8.25, true}, bL.data(), bR.data(), 2048);
    for (std::size_t i = 0; i < aL.size(); ++i) {
        REQUIRE(aL[i] == bL[i]);
        REQUIRE(aR[i] == bR[i]);
    }
}

TEST_CASE("Mono sub band below subSplit maintains >0.99 correlation even at 100% Width", "[engine]") {
    constexpr double fs = 48000.0;
    constexpr int total = 9600;
    ln::Engine eng;
    eng.prepare(fs, 256, 4);
    auto p = ln::Params::defaults();
    p[ln::ParamId::width] = 1.0f;
    p[ln::ParamId::unison] = 1.0f;
    p[ln::ParamId::detune] = 45.0f;
    p[ln::ParamId::subLevel] = 0.9f;
    p[ln::ParamId::subSplit] = 110.0f;

    eng.noteOn(29, 0.95f); // F1 = 43.65 Hz
    std::vector<float> l(total), r(total);
    for (int pos = 0; pos < total; pos += 256) {
        const int n = std::min(256, total - pos);
        eng.process(p, {172.0, 0.0, true}, l.data() + pos, r.data() + pos, n);
    }
    // Extract sub band via 2nd-order LP @ 80 Hz and measure L/R correlation
    double lpL = 0.0, lpR = 0.0;
    const double c = 1.0 - std::exp(-2.0 * 3.141592653589793 * 70.0 / fs);
    double dot = 0.0, eL = 0.0, eR = 0.0;
    for (int i = 0; i < total; ++i) {
        lpL += c * (l[static_cast<std::size_t>(i)] - lpL);
        lpR += c * (r[static_cast<std::size_t>(i)] - lpR);
        if (i >= total / 3) {
            dot += lpL * lpR;
            eL += lpL * lpL;
            eR += lpR * lpR;
        }
    }
    const double corr = dot / std::sqrt(std::max(1e-12, eL * eR));
    CHECK(corr > 0.99);
}

TEST_CASE("Strata sub-split protection, Hi/Lo band-split crossover, Loop Xfade, and Keytrack behave cleanly",
          "[engine]") {
    constexpr double fs = 48000.0;
    auto p = ln::Params::defaults();
    p[ln::ParamId::width] = 1.0f;
    p[ln::ParamId::unison] = 0.9f;
    p[ln::ParamId::detune] = 35.0f;
    p[ln::ParamId::printBars] = 0.0f; // 1 bar
    const auto gen2 = ln::Engine::renderPrint(p, ln::defaultLaneCurves(), fs, 172.0, 29);
    REQUIRE(gen2.valid());
    CHECK(gen2.label.find("F1") != std::string::npos);

    // 1) Pitched-down Strata (-12 st) must NEVER break sub-band mono correlation!
    ln::Engine eng;
    eng.prepare(fs, 256, 4);
    eng.publishPrint(gen2);
    p[ln::ParamId::genMix] = 1.0f;
    p[ln::ParamId::strataPitch] = -12.0f;
    p[ln::ParamId::subLevel] = 0.9f;
    p[ln::ParamId::subSplit] = 110.0f;
    eng.noteOn(29, 0.95f);

    constexpr int total = 9600;
    std::vector<float> l(total), r(total);
    for (int pos = 0; pos < total; pos += 256)
        eng.process(p, {172.0, 0.0, true}, l.data() + pos, r.data() + pos, std::min(256, total - pos));

    double lp1L = 0.0, lp1R = 0.0, lp2L = 0.0, lp2R = 0.0, dot = 0.0, eL = 0.0, eR = 0.0;
    const double c = 1.0 - std::exp(-2.0 * 3.141592653589793 * 70.0 / fs);
    for (int i = 0; i < total; ++i) {
        lp1L += c * (l[static_cast<std::size_t>(i)] - lp1L);
        lp1R += c * (r[static_cast<std::size_t>(i)] - lp1R);
        lp2L += c * (lp1L - lp2L);
        lp2R += c * (lp1R - lp2R);
        if (i >= total / 3) {
            dot += lp2L * lp2R;
            eL += lp2L * lp2L;
            eR += lp2R * lp2R;
        }
    }
    CHECK((dot / std::sqrt(std::max(1e-12, eL * eR))) > 0.995);

    // 2) Hi-Strata & Lo-Strata crossover modes + Scrub inertia + Keytrack Off all produce finite distinct audio
    auto renderMode = [&](ln::StrataBlend blend, ln::StrataDir dir, float fadeMs, bool keytrack) {
        ln::Engine e;
        e.prepare(fs, 256, 4);
        e.publishPrint(gen2);
        auto pm = p;
        pm[ln::ParamId::strataPitch] = 12.0f;
        pm[ln::ParamId::strataBlend] = static_cast<float>(blend);
        pm[ln::ParamId::strataDir] = static_cast<float>(dir);
        pm[ln::ParamId::strataFade] = fadeMs;
        pm[ln::ParamId::strataKeytrack] = keytrack ? 1.0f : 0.0f;
        e.noteOn(36, 0.95f);
        std::vector<float> ol(2048), or_(2048);
        e.process(pm, {172.0, 0.0, true}, ol.data(), or_.data(), 2048);
        double energy = 0.0;
        for (std::size_t i = 0; i < ol.size(); ++i) {
            REQUIRE(std::isfinite(ol[i]));
            REQUIRE(std::isfinite(or_[i]));
            energy += ol[i] * ol[i] + or_[i] * or_[i];
        }
        CHECK(std::sqrt(energy / (2.0 * static_cast<double>(ol.size()))) > 0.05);
        return ol;
    };

    const auto hi = renderMode(ln::StrataBlend::hiStrata, ln::StrataDir::forward, 24.0f, true);
    const auto lo = renderMode(ln::StrataBlend::loStrata, ln::StrataDir::scrub, 35.0f, false);
    double diff = 0.0;
    for (std::size_t i = 0; i < hi.size(); ++i)
        diff += std::abs(hi[i] - lo[i]);
    CHECK(diff > 1.0);
}
