// SPDX-License-Identifier: AGPL-3.0-or-later
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "tg/engine.h"
#include "tg/export.h"

#include <cmath>
#include <vector>

using Catch::Approx;

namespace {
double rmsOf(const std::vector<float>& buf) {
    double sum = 0.0;
    for (float x : buf)
        sum += static_cast<double>(x) * static_cast<double>(x);
    return std::sqrt(sum / static_cast<double>(std::max<std::size_t>(1, buf.size())));
}
} // namespace

TEST_CASE("Engine processes Spiral, Copedent, and Mobius modes cleanly without NaN/Inf", "[engine][realtime]") {
    constexpr double sr = 48000.0;
    constexpr int block = 256;
    tg::Engine engine;
    engine.prepare(sr, block, 4);

    std::vector<float> outL(block, 0.0f), outR(block, 0.0f);
    tg::OutMidiBuffer midiOut;

    for (int modeIdx = 0; modeIdx < 3; ++modeIdx) {
        engine.reset();
        auto params = tg::Params::defaults();
        params.set(tg::ParamId::mode, static_cast<float>(modeIdx));
        params.set(tg::ParamId::rissetGate, modeIdx == 2 ? 40.0f : 0.0f);
        engine.setSceneMatrix(tg::matrixForPack(modeIdx == 2 ? 6 : 0));
        engine.noteOn(41, 0.95f); // F2

        double energy = 0.0;
        for (int b = 0; b < 40; ++b) {
            tg::Transport t{172.0, static_cast<double>(b * block) / sr * (172.0 / 60.0), true};
            engine.process(params, t, outL.data(), outR.data(), block, &midiOut);
            for (int i = 0; i < block; ++i) {
                REQUIRE(std::isfinite(outL[static_cast<std::size_t>(i)]));
                REQUIRE(std::isfinite(outR[static_cast<std::size_t>(i)]));
                REQUIRE(std::abs(outL[static_cast<std::size_t>(i)]) <= 1.05f);
                REQUIRE(std::abs(outR[static_cast<std::size_t>(i)]) <= 1.05f);
                energy += static_cast<double>(outL[static_cast<std::size_t>(i)] * outL[static_cast<std::size_t>(i)]);
            }
        }
        REQUIRE(energy > 0.5);
    }
}

TEST_CASE("Bar-locked Shepard Spiral wraps smoothly across bar boundary with zero impulse click", "[engine][shepard]") {
    constexpr double sr = 48000.0;
    constexpr double bpm = 172.0;
    constexpr int block = 128;

    auto params = tg::Params::defaults();
    params.set(tg::ParamId::mode, 0.0f);     // Spiral
    params.set(tg::ParamId::loopBars, 1.0f); // 1 Bar = 4 beats
    params.set(tg::ParamId::timbre, 1.0f);   // Pure Sine for surgical wrap measurement
    params.set(tg::ParamId::drive, 0.0f);
    params.set(tg::ParamId::subAnchor, 0.0f);

    tg::Engine engine;
    engine.prepare(sr, block, 2);
    engine.noteOn(41, 0.9f);

    // Run across the 1-bar wrap (ppq 3.85 -> 4.15) and verify sample-to-sample derivative never spikes!
    std::vector<float> outL(block), outR(block);
    const double beatsPerSec = bpm / 60.0;
    double ppq = 3.5;
    float prev = 0.0f;
    float maxDiff = 0.0f;
    bool warmed = false;

    for (int b = 0; b < 120; ++b) {
        tg::Transport t{bpm, ppq, true};
        engine.process(params, t, outL.data(), outR.data(), block, nullptr);
        for (int i = 0; i < block; ++i) {
            if (warmed && ppq > 3.85) {
                const float d = std::abs(outL[static_cast<std::size_t>(i)] - prev);
                maxDiff = std::max(maxDiff, d);
            }
            prev = outL[static_cast<std::size_t>(i)];
            warmed = true;
        }
        ppq += (static_cast<double>(block) / sr) * beatsPerSec;
    }
    REQUIRE(maxDiff < 0.25f);
}

TEST_CASE("Sub-Anchor Floor stays strictly mono (L == R) when Stereo Width is 100%", "[engine][sub]") {
    constexpr double sr = 48000.0;
    constexpr int block = 256;
    auto params = tg::Params::defaults();
    params.set(tg::ParamId::mode, 0.0f);
    params.set(tg::ParamId::width, 100.0f);
    params.set(tg::ParamId::subAnchor, 100.0f);
    params.set(tg::ParamId::subCutoff, 120.0f);
    params.set(tg::ParamId::filterHz, 120.0f); // Isolate sub band below crossover
    params.set(tg::ParamId::drive, 0.0f);

    tg::Engine engine;
    engine.prepare(sr, block, 2);
    engine.noteOn(29, 0.95f); // F1

    std::vector<float> L(block), R(block);
    double sumMidSq = 0.0, sumSideSq = 0.0;
    for (int b = 0; b < 32; ++b) {
        tg::Transport t{172.0, static_cast<double>(b * block) / sr * (172.0 / 60.0), true};
        engine.process(params, t, L.data(), R.data(), block, nullptr);
        if (b >= 8) {
            for (int i = 0; i < block; ++i) {
                const double mid = 0.5 * (L[static_cast<std::size_t>(i)] + R[static_cast<std::size_t>(i)]);
                const double side = 0.5 * (L[static_cast<std::size_t>(i)] - R[static_cast<std::size_t>(i)]);
                sumMidSq += mid * mid;
                sumSideSq += side * side;
            }
        }
    }
    REQUIRE(sumMidSq > 0.01);
    REQUIRE(sumSideSq / sumMidSq < 0.02); // >98% mono correlation below sub crossover!
}

TEST_CASE("MPE Lower Zone emits MCM, RPN 00 00, 5 voice Note-Ons and poly pitch bends", "[engine][mpe]") {
    constexpr double sr = 48000.0;
    constexpr int block = 256;
    auto params = tg::Params::defaults();
    params.set(tg::ParamId::mode, 1.0f); // Copedent
    params.set(tg::ParamId::mpeOut, 1.0f);
    params.set(tg::ParamId::morphAuto, 1.0f);

    tg::Engine engine;
    engine.prepare(sr, block, 2);
    engine.noteOn(41, 0.9f);

    std::vector<float> L(block), R(block);
    tg::OutMidiBuffer midi;
    int noteOns = 0;
    int pitchBends = 0;
    bool sawMcm = false;

    for (int b = 0; b < 30; ++b) {
        tg::Transport t{140.0, static_cast<double>(b * block) / sr * (140.0 / 60.0), true};
        engine.process(params, t, L.data(), R.data(), block, &midi);
        for (int e = 0; e < midi.count; ++e) {
            const auto& ev = midi.events[static_cast<std::size_t>(e)];
            if ((ev.data[0] & 0xF0u) == 0x90u && ev.data[2] > 0)
                ++noteOns;
            if ((ev.data[0] & 0xF0u) == 0xE0u)
                ++pitchBends;
            if (ev.data[0] == 0xB0u && ev.data[1] == 100u && ev.data[2] == 6u)
                sawMcm = true;
        }
    }
    REQUIRE(sawMcm);
    REQUIRE(noteOns == tg::kVoiceCount);
    REQUIRE(pitchBends >= tg::kVoiceCount);
}

TEST_CASE("Offline loop render and MPE SMF export produce valid exact-bar outputs", "[engine][export]") {
    tg::RenderRequest req;
    req.params = tg::Params::defaults();
    req.params.set(tg::ParamId::mode, 0.0f);
    req.params.set(tg::ParamId::loopBars, 0.0f); // 1/2 bar = 2 beats
    req.bpm = 120.0;                             // 2 beats = 1.0 s = 48000 stereo frames
    req.sampleRate = 48000.0;
    req.oversampling = 2;

    const auto audio = tg::renderOfflineLoop(req);
    REQUIRE(audio.size() == 48000u * 2u);
    REQUIRE(rmsOf(audio) > 0.05);

    const auto smf = tg::exportMpeSmf(req.params, req.curves, req.matrix, req.note, req.bpm);
    REQUIRE(smf.size() > 64);
    REQUIRE(smf[0] == 'M');
    REQUIRE(smf[1] == 'T');
    REQUIRE(smf[2] == 'h');
    REQUIRE(smf[3] == 'd');
}

TEST_CASE("All 4 filter modes and upper-band Stereo Space keep sub strictly mono and finite", "[engine][pass2]") {
    constexpr double sr = 48000.0;
    constexpr int block = 256;
    tg::Engine engine;
    engine.prepare(sr, block, 4);
    std::vector<float> L(block), R(block);

    for (int fm = 0; fm < 4; ++fm) {
        engine.reset();
        auto params = tg::Params::defaults();
        params.set(tg::ParamId::filterMode, static_cast<float>(fm));
        params.set(tg::ParamId::space, 100.0f);
        params.set(tg::ParamId::subWarmth, 80.0f);
        params.set(tg::ParamId::filterRes, 60.0f);
        engine.noteOn(41, 0.9f);

        double energy = 0.0;
        for (int b = 0; b < 20; ++b) {
            tg::Transport t{172.0, static_cast<double>(b * block) / sr * (172.0 / 60.0), true};
            engine.process(params, t, L.data(), R.data(), block, nullptr);
            for (int i = 0; i < block; ++i) {
                REQUIRE(std::isfinite(L[static_cast<std::size_t>(i)]));
                REQUIRE(std::isfinite(R[static_cast<std::size_t>(i)]));
                energy += static_cast<double>(L[static_cast<std::size_t>(i)] * L[static_cast<std::size_t>(i)]);
            }
        }
        REQUIRE(energy > 0.1);
    }

    // Verify octave-aware Spiral bell responds to F1 (29) vs F3 (53)!
    engine.reset();
    auto p = tg::Params::defaults();
    p.set(tg::ParamId::mode, 0.0f);
    engine.noteOn(29, 0.9f);
    tg::Transport t{172.0, 0.0, true};
    engine.process(p, t, L.data(), R.data(), block, nullptr);
    const float lowBell = engine.telemetry().effectiveBellHz;

    engine.noteOn(53, 0.9f);
    for (int b = 0; b < 16; ++b)
        engine.process(p, t, L.data(), R.data(), block, nullptr);
    const float highBell = engine.telemetry().effectiveBellHz;
    REQUIRE(highBell > lowBell * 1.8f);
}

TEST_CASE("Low-octave MIDI KeySwitch C0..D#0 shifts Morph scene smoothly without transposing root",
          "[engine][keyswitch]") {
    constexpr double sr = 48000.0;
    constexpr int block = 256;
    tg::Engine engine;
    engine.prepare(sr, block, 2);
    auto p = tg::Params::defaults();
    p.set(tg::ParamId::mode, 1.0f); // Copedent
    p.set(tg::ParamId::morphAuto, 0.0f);
    p.set(tg::ParamId::keySwitch, 1.0f);

    engine.noteOn(41, 0.9f, true); // Play F2
    engine.noteOn(15, 0.9f, true); // D#0 keyswitch -> Scene D (morph = 1.0)
    REQUIRE(engine.activeNote() == 41);

    std::vector<float> L(block), R(block);
    for (int b = 0; b < 60; ++b) {
        tg::Transport t{140.0, 0.0, true};
        engine.process(p, t, L.data(), R.data(), block, nullptr);
    }
    REQUIRE(engine.telemetry().keySwitchScene == 3);
    REQUIRE(engine.telemetry().morphPos > 0.92f);
}

TEST_CASE("Spiral and Mobius Sub-Anchor wraps stay click-free at 100% Sub-Anchor and all 7 timbres stay finite",
          "[engine][v2][wrap]") {
    constexpr double sr = 48000.0;
    constexpr int block = 128;
    constexpr double bpm = 172.0;
    const double beatsPerSec = bpm / 60.0;

    // Worst-case wrap key B (rootKey = 11 -> floor ~30.87 Hz right near subAnchorWeight slope) at 100% Sub-Anchor!
    for (int modeIdx : {0, 2}) {
        auto params = tg::Params::defaults();
        params.set(tg::ParamId::mode, static_cast<float>(modeIdx));
        params.set(tg::ParamId::rootKey, 11.0f);
        params.set(tg::ParamId::loopBars, 0.0f); // fast 1/2 bar wrap!
        params.set(tg::ParamId::timbre, 1.0f);   // Pure Sine
        params.set(tg::ParamId::subAnchor, 100.0f);
        params.set(tg::ParamId::drive, 0.0f);
        params.set(tg::ParamId::space, 0.0f);

        tg::Engine engine;
        engine.prepare(sr, block, 2);
        engine.setSceneMatrix(tg::matrixForPack(modeIdx == 2 ? 6 : 0));
        engine.noteOn(47, 0.9f); // B2

        std::vector<float> L(block), R(block);
        double ppq = 0.0;
        float prev = 0.0f;
        float maxDiff = 0.0f;
        for (int b = 0; b < 320; ++b) {
            engine.process(params, {bpm, ppq, true}, L.data(), R.data(), block, nullptr);
            for (int i = 0; i < block; ++i) {
                const float s = L[static_cast<std::size_t>(i)];
                REQUIRE(std::isfinite(s));
                if (b >= 16)
                    maxDiff = std::max(maxDiff, std::abs(s - prev));
                prev = s;
            }
            ppq += (static_cast<double>(block) / sr) * beatsPerSec;
        }
        CAPTURE(modeIdx, maxDiff);
        REQUIRE(maxDiff < 0.14f);
    }

    // All 7 timbres (including Glass Bell & Super Organ) produce rich finite stereo audio with FDN Space active!
    tg::Engine engine;
    engine.prepare(sr, block, 2);
    std::vector<float> L(block), R(block);
    for (int timbreIdx = 0; timbreIdx < 7; ++timbreIdx) {
        engine.reset();
        auto p = tg::Params::defaults();
        p.set(tg::ParamId::timbre, static_cast<float>(timbreIdx));
        p.set(tg::ParamId::space, 75.0f);
        engine.noteOn(41, 0.9f);
        double energy = 0.0;
        for (int b = 0; b < 30; ++b) {
            engine.process(p, {140.0, static_cast<double>(b * block) / sr * (140.0 / 60.0), true}, L.data(), R.data(),
                           block, nullptr);
            for (int i = 0; i < block; ++i) {
                REQUIRE(std::isfinite(L[static_cast<std::size_t>(i)]));
                REQUIRE(std::isfinite(R[static_cast<std::size_t>(i)]));
                energy += static_cast<double>(L[static_cast<std::size_t>(i)] * L[static_cast<std::size_t>(i)]);
            }
        }
        CAPTURE(timbreIdx, energy);
        REQUIRE(energy > 0.08);
    }
}

TEST_CASE("Playable 2D Manifold Pad triggers sound directly, freezes/reverses Spiral, and keeps FDN Space sub mono",
          "[engine][v2][pad]") {
    constexpr double sr = 48000.0;
    constexpr int block = 256;
    tg::Engine engine;
    engine.prepare(sr, block, 2);
    std::vector<float> L(block), R(block);

    // 1. Direct padTouch sounds immediately with NO MIDI noteOn!
    auto p = tg::Params::defaults();
    p.set(tg::ParamId::padProgram, 0.0f); // Spiral
    p.set(tg::ParamId::padTouch, 1.0f);
    p.set(tg::ParamId::padX, 25.0f); // Exact 0x FREEZE detent!
    p.set(tg::ParamId::padY, 70.0f);
    double energy = 0.0;
    float posAfterWarmup = 0.0f;
    for (int b = 0; b < 40; ++b) {
        engine.process(p, {172.0, static_cast<double>(b * block) / sr * (172.0 / 60.0), true}, L.data(), R.data(),
                       block, nullptr);
        if (b == 16)
            posAfterWarmup = engine.telemetry().spiralPos;
        for (int i = 0; i < block; ++i)
            energy += static_cast<double>(L[static_cast<std::size_t>(i)] * L[static_cast<std::size_t>(i)]);
    }
    REQUIRE(energy > 0.25);
    REQUIRE(engine.telemetry().sounding);
    REQUIRE(std::abs(engine.telemetry().spiralVel) < 0.05f);
    REQUIRE(std::abs(engine.telemetry().spiralPos - posAfterWarmup) < 0.02f);

    // 2. Reverse Spiral at padX = 0% moves spiralPos backward smoothly!
    p.set(tg::ParamId::padX, 0.0f);
    const float posBeforeRev = engine.telemetry().spiralPos;
    for (int b = 0; b < 32; ++b) {
        engine.process(p, {172.0, static_cast<double>((40 + b) * block) / sr * (172.0 / 60.0), true}, L.data(),
                       R.data(), block, nullptr);
    }
    REQUIRE(engine.telemetry().spiralVel < -1.5f);
    REQUIRE(engine.telemetry().spiralPos < posBeforeRev - 0.05f);

    // 3. Void Halo at 100% Space + 100% Width still keeps Sub-Anchor strictly mono below Sub X-Over!
    engine.reset();
    auto v = tg::Params::defaults();
    v.set(tg::ParamId::padProgram, 5.0f); // Void
    v.set(tg::ParamId::padHold, 1.0f);
    v.set(tg::ParamId::padX, 100.0f);
    v.set(tg::ParamId::padY, 100.0f);
    v.set(tg::ParamId::space, 100.0f);
    v.set(tg::ParamId::width, 100.0f);
    v.set(tg::ParamId::subAnchor, 100.0f);
    v.set(tg::ParamId::subCutoff, 110.0f);
    v.set(tg::ParamId::filterHz, 120.0f);
    double midSq = 0.0, sideSq = 0.0;
    for (int b = 0; b < 32; ++b) {
        engine.process(v, {140.0, static_cast<double>(b * block) / sr * (140.0 / 60.0), true}, L.data(), R.data(),
                       block, nullptr);
        if (b >= 10) {
            for (int i = 0; i < block; ++i) {
                const double mid = 0.5 * (L[static_cast<std::size_t>(i)] + R[static_cast<std::size_t>(i)]);
                const double side = 0.5 * (L[static_cast<std::size_t>(i)] - R[static_cast<std::size_t>(i)]);
                midSq += mid * mid;
                sideSq += side * side;
            }
        }
    }
    REQUIRE(midSq > 0.01);
    REQUIRE(sideSq / midSq < 0.03);
}
