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
