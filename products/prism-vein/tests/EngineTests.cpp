// SPDX-License-Identifier: AGPL-3.0-or-later
#include "pv/engine.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

double computeRms(const std::vector<float>& buf, int start) {
    double sum = 0.0;
    int cnt = 0;
    for (int i = start; i < static_cast<int>(buf.size()); ++i) {
        const double v = buf[static_cast<std::size_t>(i)];
        sum += v * v;
        ++cnt;
    }
    return cnt > 0 ? std::sqrt(sum / static_cast<double>(cnt)) : 0.0;
}

} // namespace

TEST_CASE("Prism Vein Engine stays finite across all 4 engines, MIDI chords, sidechain, and latency modes",
          "[engine]") {
    constexpr double sr = 48000.0;
    constexpr int total = 16384;
    constexpr int block = 256;

    std::vector<float> inL(total), inR(total), scL(total), scR(total), outL(total), outR(total);
    for (int i = 0; i < total; ++i) {
        // Rich broadband saw-like input + stereo sidechain chord carrier.
        const double t = static_cast<double>(i) / sr;
        const double saw = 2.0 * (t * 55.0 - std::floor(t * 55.0)) - 1.0;
        const double sub = std::sin(hba::dsp::kTwoPi * 55.0 * t);
        inL[static_cast<std::size_t>(i)] = static_cast<float>(0.35 * saw + 0.35 * sub);
        inR[static_cast<std::size_t>(i)] = static_cast<float>(0.32 * saw + 0.35 * sub);
        scL[static_cast<std::size_t>(i)] = static_cast<float>(0.3 * std::sin(hba::dsp::kTwoPi * 349.23 * t) +
                                                              0.25 * std::sin(hba::dsp::kTwoPi * 523.25 * t));
        scR[static_cast<std::size_t>(i)] = static_cast<float>(0.3 * std::sin(hba::dsp::kTwoPi * 415.30 * t) +
                                                              0.25 * std::sin(hba::dsp::kTwoPi * 622.25 * t));
    }

    for (int latMode = 0; latMode <= 1; ++latMode) {
        for (int eng = 0; eng < 4; ++eng) {
            pv::Engine engine;
            auto p = pv::Params::defaults();
            p.set(pv::ParamId::engineMode, static_cast<float>(eng));
            p.set(pv::ParamId::latencyMode, static_cast<float>(latMode));
            p.set(pv::ParamId::color, 0.90f);
            p.set(pv::ParamId::decay, 0.55f);
            p.set(pv::ParamId::tame, 0.60f);
            p.set(pv::ParamId::grit, 0.35f);
            engine.setParams(p);
            engine.prepare(sr, block);

            CHECK(engine.latencySamples() == (latMode == 1 ? 0 : pv::Engine::kFftSize));

            // Trigger a live MIDI F min9 chord halfway through.
            for (int off = 0; off < total; off += block) {
                if (off == 4096) {
                    engine.noteOn(53, 0.9f);
                    engine.noteOn(56, 0.85f);
                    engine.noteOn(60, 0.9f);
                    engine.noteOn(63, 0.8f);
                    engine.noteOn(67, 0.85f);
                }
                const int n = std::min(block, total - off);
                engine.process(inL.data() + off, inR.data() + off, scL.data() + off, scR.data() + off,
                               outL.data() + off, outR.data() + off, n);
            }

            for (int i = 0; i < total; ++i) {
                REQUIRE(std::isfinite(outL[static_cast<std::size_t>(i)]));
                REQUIRE(std::isfinite(outR[static_cast<std::size_t>(i)]));
                REQUIRE(std::abs(outL[static_cast<std::size_t>(i)]) <= 1.2f);
                REQUIRE(std::abs(outR[static_cast<std::size_t>(i)]) <= 1.2f);
            }
            CHECK(computeRms(outL, 4096) > 0.05);
            CHECK(computeRms(outR, 4096) > 0.05);
        }
    }
}

TEST_CASE("Prism Vein Sub-Safe LR4 bypass keeps 55 Hz sub mono and phase-intact even at 100% Color", "[engine]") {
    constexpr double sr = 48000.0;
    constexpr int total = 12288;
    std::vector<float> inL(total), inR(total), outL(total), outR(total);
    for (int i = 0; i < total; ++i) {
        // Pure 55 Hz sub with opposite stereo tilt to verify subMono collapses it cleanly!
        const double s = 0.5 * std::sin(hba::dsp::kTwoPi * 55.0 * static_cast<double>(i) / sr);
        inL[static_cast<std::size_t>(i)] = static_cast<float>(s * 1.1);
        inR[static_cast<std::size_t>(i)] = static_cast<float>(s * 0.7);
    }

    pv::Engine engine;
    auto p = pv::Params::defaults();
    p.set(pv::ParamId::color, 1.0f);
    p.set(pv::ParamId::subLockHz, 120.0f);
    p.set(pv::ParamId::subMono, 1.0f);
    p.set(pv::ParamId::grit, 0.0f);
    p.set(pv::ParamId::airDb, 0.0f);
    engine.setParams(p);
    engine.prepare(sr, 256);

    for (int off = 0; off < total; off += 256)
        engine.process(inL.data() + off, inR.data() + off, nullptr, nullptr, outL.data() + off, outR.data() + off, 256);

    double maxSideErr = 0.0;
    for (int i = 4096; i < total; ++i)
        maxSideErr = std::max(maxSideErr, std::abs(static_cast<double>(outL[static_cast<std::size_t>(i)]) -
                                                   static_cast<double>(outR[static_cast<std::size_t>(i)])));

    const double rmsIn = 0.5 * (computeRms(inL, 4096) + computeRms(inR, 4096));
    const double rmsOut = computeRms(outL, 4096);
    CHECK(rmsOut == Catch::Approx(rmsIn).margin(0.06));
    CHECK(maxSideErr < 0.01);
}

TEST_CASE("Prism Vein Bark Auto-Tame actively attenuates harsh 3.4 kHz resonant spikes", "[engine]") {
    constexpr double sr = 48000.0;
    constexpr int total = 16384;
    std::vector<float> inL(total), inR(total), outNoTameL(total), outNoTameR(total), outTamedL(total), outTamedR(total);

    hba::dsp::XorShift32 rng;
    rng.seed(12345u);
    // Broadband noise bed + piercing narrowband 3492 Hz resonance (A7 / F harmonic whistle).
    for (int i = 0; i < total; ++i) {
        const double whistle = 0.48 * std::sin(hba::dsp::kTwoPi * 3492.3 * static_cast<double>(i) / sr);
        const double bed = 0.12 * rng.nextBipolar();
        inL[static_cast<std::size_t>(i)] = static_cast<float>(whistle + bed);
        inR[static_cast<std::size_t>(i)] = static_cast<float>(whistle + bed);
    }

    auto runWith = [&](float tameVal, std::vector<float>& oL, std::vector<float>& oR) -> float {
        pv::Engine eng;
        auto p = pv::Params::defaults();
        p.set(pv::ParamId::color, 1.0f);
        p.set(pv::ParamId::tame, tameVal);
        p.set(pv::ParamId::tameFocusHz, 3500.0f);
        p.set(pv::ParamId::tameSelect, 0.85f);
        p.set(pv::ParamId::grit, 0.0f);
        p.set(pv::ParamId::limitOn, 0.0f);
        p.set(pv::ParamId::airDb, 0.0f);
        eng.setParams(p);
        eng.prepare(sr, 256);
        for (int off = 0; off < total; off += 256)
            eng.process(inL.data() + off, inR.data() + off, nullptr, nullptr, oL.data() + off, oR.data() + off, 256);
        return eng.telemetry().tamePeakDb;
    };

    const float cutOff = runWith(0.0f, outNoTameL, outNoTameR);
    const float cutOn = runWith(1.0f, outTamedL, outTamedR);

    CHECK(cutOff < 0.1f);
    CHECK(cutOn > 2.5f);
    CHECK(computeRms(outTamedL, 4096) < computeRms(outNoTameL, 4096) * 0.92);
}

TEST_CASE("Prism Vein Demo Audition generates stereo audio without external input", "[engine]") {
    pv::Engine engine;
    auto p = pv::Params::defaults();
    p.set(pv::ParamId::demoAudition, 1.0f);
    engine.setParams(p);
    engine.prepare(48000.0, 256);

    std::vector<float> zero(256, 0.0f), outL(8192, 0.0f), outR(8192, 0.0f);
    for (int off = 0; off < 8192; off += 256)
        engine.process(zero.data(), zero.data(), nullptr, nullptr, outL.data() + off, outR.data() + off, 256);

    CHECK(computeRms(outL, 2048) > 0.04);
    CHECK(computeRms(outR, 2048) > 0.04);
}
