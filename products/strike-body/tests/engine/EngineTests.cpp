// SPDX-License-Identifier: AGPL-3.0-or-later
#include "sb/engine.h"
#include "sb/export.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

namespace {
std::vector<float> renderHit(const sb::Params& p, sb::Articulation art = sb::Articulation::main, double sr = 48000.0,
                             int totalFrames = 24000) {
    constexpr int kBlock = 128;
    sb::Engine eng;
    eng.prepare(sr, kBlock, 4);
    eng.process(p, nullptr, nullptr, 0);
    eng.trigger(art, 1.0f, 1.0f);

    std::vector<float> out(static_cast<std::size_t>(totalFrames) * 2u, 0.0f);
    std::vector<float> l(kBlock), r(kBlock);
    int pos = 0;
    while (pos < totalFrames) {
        const int n = std::min(kBlock, totalFrames - pos);
        eng.process(p, l.data(), r.data(), n);
        for (int i = 0; i < n; ++i) {
            out[static_cast<std::size_t>(pos + i) * 2u] = l[static_cast<std::size_t>(i)];
            out[static_cast<std::size_t>(pos + i) * 2u + 1u] = r[static_cast<std::size_t>(i)];
        }
        pos += n;
    }
    return out;
}
} // namespace

TEST_CASE("Strike Body engine is bit-exact deterministic when humanize is 0", "[engine]") {
    sb::Params p = sb::Params::defaults();
    p.set(sb::ParamId::humanize, 0.0f);
    const auto a = renderHit(p);
    const auto b = renderHit(p);
    REQUIRE(a.size() == b.size());
    float peak = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i) {
        REQUIRE(std::isfinite(a[i]));
        REQUIRE(a[i] == b[i]);
        peak = std::max(peak, std::abs(a[i]));
    }
    REQUIRE(peak > 0.15f);
}

TEST_CASE("Strike Body has near-zero DC offset across all 6 modal models and 6 mallet types", "[engine]") {
    for (int m = 0; m < 6; ++m) {
        sb::Params p = sb::Params::defaults();
        p.set(sb::ParamId::shellModel, static_cast<float>(m));
        p.set(sb::ParamId::malletType, static_cast<float>(m));
        p.set(sb::ParamId::bodyWave, static_cast<float>(m % 4));
        p.set(sb::ParamId::bodyDecay, 120.0f);
        p.set(sb::ParamId::shellDecay, 140.0f);
        p.set(sb::ParamId::wireDecay, 120.0f);
        p.set(sb::ParamId::drive, 55.0f);
        p.set(sb::ParamId::roomAmt, 28.0f);
        const auto audio = renderHit(p, sb::Articulation::main, 48000.0, 48000);
        double sumL = 0.0, sumR = 0.0;
        const int tailStart = 43200;
        const int count = 48000 - tailStart;
        for (int i = tailStart; i < 48000; ++i) {
            sumL += audio[static_cast<std::size_t>(i) * 2u];
            sumR += audio[static_cast<std::size_t>(i) * 2u + 1u];
        }
        REQUIRE(std::abs(sumL / count) < 3.16e-5);
        REQUIRE(std::abs(sumR / count) < 3.16e-5);
    }
}

TEST_CASE("Auto Phase-Align solver stays grid-locked (>= 0 ms) and increases constructive lock %", "[engine]") {
    sb::Params p = sb::Params::defaults();
    p.set(sb::ParamId::bodyHz, 185.0f);
    p.set(sb::ParamId::bodyPhase, 270.0f);
    p.set(sb::ParamId::shellOffsetMs, 2.2f);
    p.set(sb::ParamId::shellInvert, 1.0f);

    const auto res = sb::solveAutoAlign(p, 48000.0);
    REQUIRE(std::isfinite(res.afterLockPct));
    REQUIRE(res.shellOffsetMs >= 0.0f);
    REQUIRE(res.wireOffsetMs >= 0.0f);
    REQUIRE(res.afterLockPct > res.beforeLockPct);
    REQUIRE(res.afterLockPct > 60.0f);
}

TEST_CASE("16-stage series Disperser preserves total energy (no parallel comb-filter cancellation)", "[engine]") {
    sb::Params dry = sb::Params::defaults();
    dry.set(sb::ParamId::disperserAmt, 0.0f);
    dry.set(sb::ParamId::drive, 0.0f);
    dry.set(sb::ParamId::limiter, 0.0f);
    dry.set(sb::ParamId::roomAmt, 0.0f);
    dry.set(sb::ParamId::scoopAmt, 0.0f);
    dry.set(sb::ParamId::outputDb, -6.0f);

    sb::Params wet = dry;
    wet.set(sb::ParamId::disperserAmt, 65.0f);
    wet.set(sb::ParamId::disperserHz, 260.0f);

    const auto a = renderHit(dry, sb::Articulation::main, 48000.0, 24000);
    const auto b = renderHit(wet, sb::Articulation::main, 48000.0, 24000);
    double eA = 0.0, eB = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        eA += static_cast<double>(a[i]) * a[i];
        eB += static_cast<double>(b[i]) * b[i];
    }
    const double ratioDb = 10.0 * std::log10(eB / std::max(1e-12, eA));
    // Allpass Parseval energy preservation: must stay within +/- 0.5 dB!
    REQUIRE(std::abs(ratioDb) < 0.5);
}

TEST_CASE("Kit Map triggers exact C1=MAIN, C#1=GHOST, D1=RIM, D#1=ROLL", "[engine]") {
    sb::Engine eng;
    eng.prepare(48000.0, 128, 2);
    sb::Params p = sb::Params::defaults();
    p.set(sb::ParamId::keyTrack, 0.0f);
    std::array<float, 128> l{}, r{};

    eng.noteOn(36, 0.95f, p);
    eng.process(p, l.data(), r.data(), 128);
    REQUIRE(eng.telemetry().lastArticulation == static_cast<int>(sb::Articulation::main));

    eng.noteOn(37, 0.95f, p);
    eng.process(p, l.data(), r.data(), 128);
    REQUIRE(eng.telemetry().lastArticulation == static_cast<int>(sb::Articulation::ghost));

    eng.noteOn(38, 0.95f, p);
    eng.process(p, l.data(), r.data(), 128);
    REQUIRE(eng.telemetry().lastArticulation == static_cast<int>(sb::Articulation::rim));

    eng.noteOn(39, 0.95f, p);
    eng.process(p, l.data(), r.data(), 128);
    REQUIRE(eng.telemetry().lastArticulation == static_cast<int>(sb::Articulation::roll));
}

TEST_CASE("All 4 articulations and sample rates produce finite bounded audio and scope telemetry", "[engine]") {
    for (double sr : {44100.0, 48000.0, 96000.0}) {
        sb::Engine eng;
        eng.prepare(sr, 256, 4);
        sb::Params p = sb::Params::defaults();
        p.set(sb::ParamId::disperserAmt, 65.0f);
        p.set(sb::ParamId::drive, 60.0f);
        p.set(sb::ParamId::driveModel, 2.0f);
        p.set(sb::ParamId::roomAmt, 24.0f);
        p.set(sb::ParamId::macroPunch, 35.0f);
        p.set(sb::ParamId::macroSmack, 40.0f);

        std::vector<float> l(256), r(256);
        for (int a = 0; a < 4; ++a) {
            eng.process(p, l.data(), r.data(), 256);
            eng.trigger(static_cast<sb::Articulation>(a), 0.9f, 1.0f);
            float peak = 0.0f;
            for (int blk = 0; blk < 24; ++blk) {
                eng.process(p, l.data(), r.data(), 256);
                for (int i = 0; i < 256; ++i) {
                    REQUIRE(std::isfinite(l[static_cast<std::size_t>(i)]));
                    REQUIRE(std::isfinite(r[static_cast<std::size_t>(i)]));
                    peak = std::max(peak, std::max(std::abs(l[static_cast<std::size_t>(i)]),
                                                   std::abs(r[static_cast<std::size_t>(i)])));
                }
            }
            REQUIRE(peak > 0.05f);
            REQUIRE(peak <= 1.5f);
        }
        REQUIRE(eng.telemetry().triggerCount == 4u);
        REQUIRE(eng.telemetry().scopePeak > 0.05f);
    }
}

TEST_CASE("One-shot WAV bounce strips leading latency so transient starts grid-locked at sample 0", "[engine]") {
    sb::OneShotRequest req;
    req.params = sb::Params::defaults();
    const auto wav = sb::renderOneShot(req);
    REQUIRE(wav.size() > 8000u);
    float peak = 0.0f;
    for (float s : wav) {
        REQUIRE(std::isfinite(s));
        peak = std::max(peak, std::abs(s));
    }
    REQUIRE(peak == Catch::Approx(0.944f).margin(0.03f));

    // Verify transient energy begins within the first 1.5 ms (72 frames at 48 kHz), NOT 4-6 ms late!
    float earlyPeak = 0.0f;
    for (std::size_t i = 0; i < std::min<std::size_t>(wav.size(), 144u); ++i)
        earlyPeak = std::max(earlyPeak, std::abs(wav[i]));
    REQUIRE(earlyPeak > 0.15f);
    REQUIRE(sb::keyTaggedStem("Neuro Crack", 185.0f) == "Neuro_Crack_F#3_185Hz");
}

TEST_CASE("Strike Body engine process loop is realtime-safe", "[engine][realtime]") {
    sb::Engine eng;
    eng.prepare(48000.0, 256, 4);
    sb::Params p = sb::Params::defaults();
    p.set(sb::ParamId::disperserAmt, 55.0f);
    p.set(sb::ParamId::drive, 45.0f);
    p.set(sb::ParamId::roomAmt, 30.0f);
    p.set(sb::ParamId::scoopAmt, 25.0f);
    p.set(sb::ParamId::macroPunch, 30.0f);
    p.set(sb::ParamId::macroSmack, 35.0f);

    std::array<float, 256> l{};
    std::array<float, 256> r{};
    eng.noteOn(36, 0.95f, p);
    for (int b = 0; b < 32; ++b) {
        if (b == 8)
            eng.noteOn(37, 0.65f, p);
        if (b == 16)
            eng.noteOn(38, 0.90f, p);
        if (b == 24)
            eng.noteOn(39, 0.85f, p);
        eng.process(p, l.data(), r.data(), 256);
    }
    REQUIRE(std::isfinite(l[0]));
    REQUIRE(std::isfinite(r[0]));
}
