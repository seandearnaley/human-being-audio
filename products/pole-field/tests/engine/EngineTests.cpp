#include "pf/engine.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <tuple>
#include <vector>

namespace {
constexpr double kTwoPi = 6.28318530717958647692;

std::uint32_t nextRand(std::uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return s;
}

float randFloat(std::uint32_t& s) { return static_cast<float>(nextRand(s) >> 8) * (1.0f / 16777216.0f); }
}

TEST_CASE("Block-size independence across regular and irregular host buffers", "[engine]") {
    constexpr double fs = 48000.0;
    constexpr int total = 2048;

    auto params = pf::Params::defaults();
    params[pf::ParamId::motion] = 0.85f;
    params[pf::ParamId::drive] = 0.55f;
    params[pf::ParamId::spread] = 0.4f;

    std::vector<float> srcL(total), srcR(total);
    for (int i = 0; i < total; ++i) {
        const double t = static_cast<double>(i) / fs;
        srcL[static_cast<std::size_t>(i)] = static_cast<float>(0.4 * std::sin(kTwoPi * 55.0 * t)
            + 0.35 * std::sin(kTwoPi * 330.0 * t) + 0.2 * std::sin(kTwoPi * 1140.0 * t));
        srcR[static_cast<std::size_t>(i)] = static_cast<float>(0.4 * std::sin(kTwoPi * 55.0 * t)
            + 0.35 * std::sin(kTwoPi * 332.0 * t) + 0.2 * std::sin(kTwoPi * 1135.0 * t));
    }

    auto renderWithChunk = [&](int chunk) {
        pf::Engine eng;
        eng.prepare(fs, 512, 4);
        std::vector<float> outL = srcL, outR = srcR;
        const double qps = 172.0 / 60.0 / fs;
        for (int pos = 0; pos < total; pos += chunk) {
            const int n = std::min(chunk, total - pos);
            pf::Transport tr { 172.0, pos * qps, true };
            eng.process(params, tr, outL.data() + pos, outR.data() + pos, n);
        }
        return std::pair { outL, outR };
    };

    const auto [refL, refR] = renderWithChunk(512);
    for (int chunk : {1, 7, 64, 257}) {
        const auto [curL, curR] = renderWithChunk(chunk);
        for (int i = 0; i < total; ++i) {
            REQUIRE(curL[static_cast<std::size_t>(i)] == refL[static_cast<std::size_t>(i)]);
            REQUIRE(curR[static_cast<std::size_t>(i)] == refR[static_cast<std::size_t>(i)]);
        }
    }
}

TEST_CASE("BIBO stability under extreme audio-rate X/Y/Z sweeps and full resonance", "[engine]") {
    constexpr double fs = 48000.0;
    pf::Engine eng;
    eng.prepare(fs, 128, 4);

    std::uint32_t seed = 0x1337c0deu;
    std::array<float, 128> bufL {}, bufR {};
    auto params = pf::Params::defaults();
    params[pf::ParamId::res] = 1.0f;
    params[pf::ParamId::drive] = 1.0f;
    params[pf::ParamId::motion] = 1.0f;
    params[pf::ParamId::limit] = 1.0f;
    params[pf::ParamId::ceiling] = -0.3f;

    float maxAbs = 0.0f;
    for (int block = 0; block < 400; ++block) {
        params[pf::ParamId::morphX] = randFloat(seed);
        params[pf::ParamId::morphY] = randFloat(seed);
        params[pf::ParamId::morphZ] = randFloat(seed);
        params[pf::ParamId::freq] = (randFloat(seed) * 2.0f - 1.0f) * 36.0f;
        params[pf::ParamId::transform] = randFloat(seed) * 2.0f - 1.0f;
        params[pf::ParamId::cube] = static_cast<float>(block % pf::kNumCubes);
        params[pf::ParamId::topology] = static_cast<float>(block % 3);
        params[pf::ParamId::drivePos] = static_cast<float>((block / 3) % 3);
        params[pf::ParamId::foldType] = static_cast<float>((block / 9) % 5);

        for (int i = 0; i < 128; ++i) {
            bufL[static_cast<std::size_t>(i)] = randFloat(seed) * 2.0f - 1.0f;
            bufR[static_cast<std::size_t>(i)] = randFloat(seed) * 2.0f - 1.0f;
        }
        pf::Transport tr { 174.0, block * 0.1, true };
        eng.process(params, tr, bufL.data(), bufR.data(), 128);
        for (int i = 0; i < 128; ++i) {
            const float l = bufL[static_cast<std::size_t>(i)];
            const float r = bufR[static_cast<std::size_t>(i)];
            REQUIRE(std::isfinite(l));
            REQUIRE(std::isfinite(r));
            maxAbs = std::max(maxAbs, std::max(std::abs(l), std::abs(r)));
        }
    }
    REQUIRE(maxAbs <= 1.01f);
}

TEST_CASE("Sub Lock preserves mono low end while Stereo Spread widens mids", "[engine]") {
    constexpr double fs = 48000.0;
    constexpr int total = 8192;

    pf::Engine eng;
    eng.prepare(fs, 256, 4);

    auto params = pf::Params::defaults();
    params[pf::ParamId::subLock] = 1.0f;
    params[pf::ParamId::subSplit] = 140.0f;
    params[pf::ParamId::spread] = 1.0f;
    params[pf::ParamId::drive] = 0.7f;
    params[pf::ParamId::motion] = 0.0f;
    params[pf::ParamId::limit] = 0.0f;

    // Feed a stereo-offset 45 Hz sub tone (well below 140 Hz split):
    // Sub Lock should collapse low energy strictly to mono.
    std::vector<float> bufL(total), bufR(total);
    for (int i = 0; i < total; ++i) {
        const double t = static_cast<double>(i) / fs;
        bufL[static_cast<std::size_t>(i)] = static_cast<float>(0.5 * std::sin(kTwoPi * 45.0 * t));
        bufR[static_cast<std::size_t>(i)] = static_cast<float>(0.3 * std::sin(kTwoPi * 45.0 * t + 0.4));
    }

    for (int pos = 0; pos < total; pos += 256) {
        pf::Transport tr { 172.0, 0.0, true };
        eng.process(params, tr, bufL.data() + pos, bufR.data() + pos, 256);
    }

    double midEnergy = 0.0, sideEnergy = 0.0;
    for (int i = total / 2; i < total; ++i) {
        const double l = bufL[static_cast<std::size_t>(i)];
        const double r = bufR[static_cast<std::size_t>(i)];
        const double m = 0.5 * (l + r);
        const double s = 0.5 * (l - r);
        midEnergy += m * m;
        sideEnergy += s * s;
    }
    const double sideRelDb = 10.0 * std::log10(std::max(sideEnergy, 1e-18) / std::max(midEnergy, 1e-18));
    REQUIRE(midEnergy > 10.0);
    REQUIRE(sideRelDb < -55.0);
}

TEST_CASE("Modulating Drive with maximum Asym on silence emits no CV thump", "[engine]") {
    constexpr double fs = 48000.0;
    constexpr int total = 4096;

    for (int f = 0; f < static_cast<int>(pf::choices::foldType.size()); ++f) {
        pf::Engine eng;
        eng.prepare(fs, 128, 4);

        auto params = pf::Params::defaults();
        params[pf::ParamId::foldType] = static_cast<float>(f);
        params[pf::ParamId::drivePos] = static_cast<float>(pf::DrivePos::inter);
        params[pf::ParamId::drive] = 0.5f;
        params[pf::ParamId::asym] = 1.0f;
        params[pf::ParamId::motion] = 1.0f;
        params[pf::ParamId::lane1On] = 1.0f;
        params[pf::ParamId::lane1Target] = static_cast<float>(pf::LaneTarget::drive);
        params[pf::ParamId::lane1Rate] = 9.0f;  // 1/16 fast sweep
        params[pf::ParamId::lane1Depth] = 1.0f;
        params[pf::ParamId::limit] = 0.0f;

        std::vector<float> bufL(total, 0.0f), bufR(total, 0.0f);
        const double qps = 172.0 / 60.0 / fs;
        for (int pos = 0; pos < total; pos += 128) {
            pf::Transport tr { 172.0, pos * qps, true };
            eng.process(params, tr, bufL.data() + pos, bufR.data() + pos, 128);
        }

        double maxAbs = 0.0;
        for (int i = 0; i < total; ++i)
            maxAbs = std::max(maxAbs, std::max<double>(std::abs(bufL[static_cast<std::size_t>(i)]),
                                                       std::abs(bufR[static_cast<std::size_t>(i)])));
        INFO("foldType=" << pf::choices::foldType[static_cast<std::size_t>(f)] << " peak=" << maxAbs);
        REQUIRE(maxAbs < 1.0e-4);
    }
}

TEST_CASE("Live Shaper switching across all 5 ADAA2 models is click-free", "[engine]") {
    constexpr double fs = 48000.0;
    constexpr int block = 256;

    pf::Engine eng;
    eng.prepare(fs, block, 4);

    auto params = pf::Params::defaults();
    params[pf::ParamId::drive] = 0.8f;
    params[pf::ParamId::asym] = 0.35f;
    params[pf::ParamId::drivePos] = static_cast<float>(pf::DrivePos::post);
    params[pf::ParamId::motion] = 0.0f;
    params[pf::ParamId::limit] = 0.0f;

    float prevL = 0.0f;
    float maxBoundaryJump = 0.0f;
    int samplePos = 0;
    for (int f = 0; f < static_cast<int>(pf::choices::foldType.size()); ++f) {
        params[pf::ParamId::foldType] = static_cast<float>(f);
        for (int b = 0; b < 4; ++b) {
            std::array<float, block> bufL {}, bufR {};
            for (int i = 0; i < block; ++i) {
                const double t = static_cast<double>(samplePos++) / fs;
                const float s = static_cast<float>(0.45 * std::sin(kTwoPi * 110.0 * t));
                bufL[static_cast<std::size_t>(i)] = s;
                bufR[static_cast<std::size_t>(i)] = s;
            }
            pf::Transport tr { 172.0, 0.0, true };
            eng.process(params, tr, bufL.data(), bufR.data(), block);
            if (f > 0 && b == 0) maxBoundaryJump = std::max(maxBoundaryJump, std::abs(bufL[0] - prevL));
            prevL = bufL[block - 1];
        }
    }
    REQUIRE(maxBoundaryJump < 0.12f);
}

TEST_CASE("MIDI expression (CC1, CC74, aftertouch) modulates X/Y/Z and Drive smoothly and is bit-identical when idle",
          "[engine]") {
    constexpr double fs = 48000.0;
    constexpr int total = 4096;

    auto params = pf::Params::defaults();
    params[pf::ParamId::motion] = 0.0f;
    params[pf::ParamId::morphX] = 0.45f;
    params[pf::ParamId::morphY] = 0.30f;
    params[pf::ParamId::morphZ] = 0.25f;
    params[pf::ParamId::drive] = 0.30f;

    std::vector<float> srcL(total), srcR(total);
    for (int i = 0; i < total; ++i) {
        const double t = static_cast<double>(i) / fs;
        const float s = static_cast<float>(0.35 * std::sin(kTwoPi * 110.0 * t) + 0.25 * std::sin(kTwoPi * 520.0 * t));
        srcL[static_cast<std::size_t>(i)] = s;
        srcR[static_cast<std::size_t>(i)] = s;
    }

    auto renderWith = [&](auto configure) {
        pf::Engine eng;
        eng.prepare(fs, 256, 4);
        configure(eng);
        std::vector<float> outL = srcL, outR = srcR;
        for (int pos = 0; pos < total; pos += 256) {
            pf::Transport tr{172.0, 0.0, false};
            eng.process(params, tr, outL.data() + pos, outR.data() + pos, 256);
        }
        return std::tuple{outL, outR, eng.effectiveState()};
    };

    const auto [idleL, idleR, idleState] = renderWith([](pf::Engine&) {});
    const auto [neutL, neutR, neutState] = renderWith([](pf::Engine& e) {
        e.setModWheel(0.0f);
        e.setTimbre(0.5f);
        e.setAftertouch(0.0f);
    });
    for (int i = 0; i < total; ++i) {
        REQUIRE(idleL[static_cast<std::size_t>(i)] == neutL[static_cast<std::size_t>(i)]);
        REQUIRE(idleR[static_cast<std::size_t>(i)] == neutR[static_cast<std::size_t>(i)]);
    }
    CHECK(neutState.morphX == Catch::Approx(idleState.morphX));

    const auto [modL, modR, modState] = renderWith([](pf::Engine& e) {
        e.setTimbre(1.0f);     // +0.45 on Morph X
        e.setModWheel(1.0f);   // +0.45 on Morph Y
        e.setAftertouch(1.0f); // +0.40 on Morph Z + +0.25 Drive
    });
    CHECK(modState.morphX == Catch::Approx(0.90f).margin(0.02f));
    CHECK(modState.morphY == Catch::Approx(0.75f).margin(0.02f));
    CHECK(modState.morphZ == Catch::Approx(0.65f).margin(0.02f));
    CHECK(modState.drive == Catch::Approx(0.55f).margin(0.02f));

    double maxDiff = 0.0;
    for (int i = 0; i < total; ++i) {
        REQUIRE(std::isfinite(modL[static_cast<std::size_t>(i)]));
        REQUIRE(std::isfinite(modR[static_cast<std::size_t>(i)]));
        maxDiff = std::max(maxDiff, static_cast<double>(std::abs(modL[static_cast<std::size_t>(i)] -
                                                                 idleL[static_cast<std::size_t>(i)])));
    }
    CHECK(maxDiff > 1e-2);
}

TEST_CASE("Pitch bend responds quickly even with Pitch track OFF and 300 ms Glide", "[engine]") {
    constexpr double fs = 48000.0;
    pf::Engine eng;
    eng.prepare(fs, 256, 4);

    auto params = pf::Params::defaults();
    params[pf::ParamId::pitchTrack] = 0.0f; // OFF
    params[pf::ParamId::glide] = 300.0f;    // slow note glide
    params[pf::ParamId::motion] = 0.0f;
    eng.pitchBend(12.0f);

    std::array<float, 256> l{}, r{};
    // 10 blocks = ~53 ms: fast pitch bend smoother reaches near +12 st despite 300 ms note glide.
    for (int b = 0; b < 10; ++b) {
        l.fill(0.0f);
        r.fill(0.0f);
        eng.process(params, {172.0, 0.0, false}, l.data(), r.data(), 256);
    }
    CHECK(eng.effectiveState().midiShiftSt > 11.0f);
}

TEST_CASE("Limit OFF decouples headroom from Ceiling while preserving +12 dBFS safety bound", "[engine]") {
    constexpr double fs = 48000.0;
    constexpr int total = 4096;

    // 1) +4 dBFS dry signal with Limit OFF and Ceiling = -12 dB passes unclipped (> 1.45 linear).
    {
        pf::Engine eng;
        eng.prepare(fs, 256, 4);
        auto params = pf::Params::defaults();
        params[pf::ParamId::limit] = 0.0f;
        params[pf::ParamId::ceiling] = -12.0f;
        params[pf::ParamId::mix] = 0.0f;

        std::vector<float> l(total), r(total);
        for (int i = 0; i < total; ++i) {
            const float s = static_cast<float>(1.585 * std::sin(kTwoPi * 440.0 * static_cast<double>(i) / fs));
            l[static_cast<std::size_t>(i)] = s;
            r[static_cast<std::size_t>(i)] = s;
        }
        for (int pos = 0; pos < total; pos += 256)
            eng.process(params, {172.0, 0.0, false}, l.data() + pos, r.data() + pos, 256);

        float peak = 0.0f;
        for (int i = total / 2; i < total; ++i)
            peak = std::max(peak, std::abs(l[static_cast<std::size_t>(i)]));
        REQUIRE(peak > 1.45f);
        REQUIRE(peak < 1.70f);
    }

    // 2) Extreme +24 dB input + +24 dB output with Limit OFF stays safely capped under +12 dBFS (~4.05).
    {
        pf::Engine eng;
        eng.prepare(fs, 256, 4);
        auto params = pf::Params::defaults();
        params[pf::ParamId::limit] = 0.0f;
        params[pf::ParamId::inputGain] = 24.0f;
        params[pf::ParamId::outputGain] = 24.0f;
        params[pf::ParamId::mix] = 0.0f;

        std::vector<float> l(total), r(total);
        for (int i = 0; i < total; ++i) {
            const float s = static_cast<float>(std::sin(kTwoPi * 440.0 * static_cast<double>(i) / fs));
            l[static_cast<std::size_t>(i)] = s;
            r[static_cast<std::size_t>(i)] = s;
        }
        for (int pos = 0; pos < total; pos += 256)
            eng.process(params, {172.0, 0.0, false}, l.data() + pos, r.data() + pos, 256);

        float peak = 0.0f;
        for (int i = 0; i < total; ++i) {
            REQUIRE(std::isfinite(l[static_cast<std::size_t>(i)]));
            peak = std::max(peak, std::abs(l[static_cast<std::size_t>(i)]));
        }
        REQUIRE(peak <= 4.05f);
    }
}

TEST_CASE("Fit 355->5 deg wrap and live Cube/Topology/Stages/DrivePos/Routing switches are click-free", "[engine]") {
    constexpr double fs = 48000.0;
    constexpr int block = 256;

    pf::Engine eng;
    eng.prepare(fs, block, 4);

    auto params = pf::Params::defaults();
    params[pf::ParamId::motion] = 0.0f;
    params[pf::ParamId::limit] = 0.0f;
    params[pf::ParamId::subLock] = 1.0f;
    params[pf::ParamId::fit] = 355.0f;

    int samplePos = 0;
    float lastSample = 0.0f;
    bool hasLast = false;
    auto runBlock = [&](int blocks) {
        float maxStep = 0.0f;
        int maxIdx = -1;
        float prevAtMax = 0.0f, curAtMax = 0.0f;
        for (int b = 0; b < blocks; ++b) {
            std::array<float, block> l{}, r{};
            for (int i = 0; i < block; ++i) {
                const double t = static_cast<double>(samplePos++) / fs;
                const float s =
                    static_cast<float>(0.35 * std::sin(kTwoPi * 60.0 * t) + 0.25 * std::sin(kTwoPi * 440.0 * t));
                l[static_cast<std::size_t>(i)] = s;
                r[static_cast<std::size_t>(i)] = s;
            }
            eng.process(params, {172.0, 0.0, false}, l.data(), r.data(), block);
            for (int i = 0; i < block; ++i) {
                const float cur = l[static_cast<std::size_t>(i)];
                REQUIRE(std::isfinite(cur));
                if (hasLast) {
                    const float step = std::abs(cur - lastSample);
                    if (step > maxStep) {
                        maxStep = step;
                        maxIdx = b * block + i;
                        prevAtMax = lastSample;
                        curAtMax = cur;
                    }
                }
                lastSample = cur;
                hasLast = true;
            }
        }
        return std::tuple{maxStep, maxIdx, prevAtMax, curAtMax};
    };

    runBlock(6);                     // settle
    params[pf::ParamId::fit] = 5.0f; // wraps across 360->0 boundary
    {
        const auto [step, idx, p, c] = runBlock(4);
        INFO("Fit wrap: idx=" << idx << " prev=" << p << " cur=" << c);
        CHECK(step < 0.14f);
    }

    params[pf::ParamId::cube] = 5.0f; // Acid Ladder
    {
        const auto [step, idx, p, c] = runBlock(4);
        INFO("Cube switch: idx=" << idx << " prev=" << p << " cur=" << c);
        CHECK(step < 0.22f);
    }

    params[pf::ParamId::topology] = 2.0f; // Parallel
    {
        const auto [step, idx, p, c] = runBlock(4);
        INFO("Topology switch: idx=" << idx << " prev=" << p << " cur=" << c);
        CHECK(step < 0.22f);
    }

    params[pf::ParamId::stages] = 1.0f; // 8P
    {
        const auto [step, idx, p, c] = runBlock(4);
        INFO("Stages switch: idx=" << idx << " prev=" << p << " cur=" << c);
        CHECK(step < 0.22f);
    }

    params[pf::ParamId::drivePos] = 2.0f; // Post
    {
        const auto [step, idx, p, c] = runBlock(4);
        INFO("DrivePos switch: idx=" << idx << " prev=" << p << " cur=" << c);
        CHECK(step < 0.22f);
    }

    params[pf::ParamId::stereoMode] = 1.0f; // Mid
    {
        const auto [step, idx, p, c] = runBlock(4);
        INFO("Routing switch: idx=" << idx << " prev=" << p << " cur=" << c);
        CHECK(step < 0.22f);
    }
}

TEST_CASE("Envelope Follower tracks main Input and Sidechain bus independently", "[engine]") {
    constexpr double fs = 48000.0;
    constexpr int block = 256;

    auto params = pf::Params::defaults();
    params[pf::ParamId::motion] = 1.0f;
    params[pf::ParamId::morphX] = 0.2f;
    params[pf::ParamId::envAttack] = 2.0f;
    params[pf::ParamId::envRelease] = 60.0f;
    params[pf::ParamId::envSens] = 6.0f;
    params[pf::ParamId::lane1On] = 1.0f;
    params[pf::ParamId::lane1Target] = static_cast<float>(pf::LaneTarget::morphX);
    params[pf::ParamId::lane1Shape] = static_cast<float>(pf::LaneShape::env);
    params[pf::ParamId::lane1Depth] = 0.8f;
    params[pf::ParamId::lane2On] = 0.0f;

    // Input mode responds to main signal
    {
        pf::Engine eng;
        eng.prepare(fs, block, 4);
        params[pf::ParamId::envSource] = static_cast<float>(pf::EnvSource::input);
        std::array<float, block> l{}, r{};
        for (int b = 0; b < 12; ++b) {
            for (int i = 0; i < block; ++i) {
                const float s =
                    static_cast<float>(0.6 * std::sin(kTwoPi * 220.0 * static_cast<double>(b * block + i) / fs));
                l[static_cast<std::size_t>(i)] = s;
                r[static_cast<std::size_t>(i)] = s;
            }
            eng.process(params, {172.0, 0.0, false}, l.data(), r.data(), block);
        }
        CHECK(eng.effectiveState().envLevel > 0.55f);
        CHECK(eng.effectiveState().morphX > 0.40f);
    }

    // Sidechain mode ignores loud main input when sidechain is silent, then rises when sidechain hits.
    {
        pf::Engine eng;
        eng.prepare(fs, block, 4);
        params[pf::ParamId::envSource] = static_cast<float>(pf::EnvSource::sidechain);
        std::array<float, block> l{}, r{}, scSilent{}, scLoud{};
        for (int b = 0; b < 10; ++b) {
            for (int i = 0; i < block; ++i) {
                const float s =
                    static_cast<float>(0.7 * std::sin(kTwoPi * 220.0 * static_cast<double>(b * block + i) / fs));
                l[static_cast<std::size_t>(i)] = s;
                r[static_cast<std::size_t>(i)] = s;
            }
            eng.process(params, {172.0, 0.0, false}, l.data(), r.data(), block, scSilent.data(), scSilent.data());
        }
        CHECK(eng.effectiveState().envLevel < 0.02f);

        for (int b = 0; b < 12; ++b) {
            for (int i = 0; i < block; ++i) {
                const float s =
                    static_cast<float>(0.7 * std::sin(kTwoPi * 220.0 * static_cast<double>(b * block + i) / fs));
                l[static_cast<std::size_t>(i)] = s;
                r[static_cast<std::size_t>(i)] = s;
                scLoud[static_cast<std::size_t>(i)] = s;
            }
            eng.process(params, {172.0, 0.0, false}, l.data(), r.data(), block, scLoud.data(), scLoud.data());
        }
        CHECK(eng.effectiveState().envLevel > 0.55f);
    }
}

TEST_CASE("Audio-rate Filter FM (Input, Sidechain, Pitch Sine) modulates cutoff stably", "[engine]") {
    constexpr double fs = 48000.0;
    constexpr int total = 4096;

    auto renderFm = [&](float fmAmt, pf::FmSource src) {
        pf::Engine eng;
        eng.prepare(fs, 256, 4);
        auto params = pf::Params::defaults();
        params[pf::ParamId::motion] = 0.0f;
        params[pf::ParamId::fmAmount] = fmAmt;
        params[pf::ParamId::fmSource] = static_cast<float>(src);
        params[pf::ParamId::limit] = 0.0f;
        std::vector<float> l(total), r(total), sc(total);
        for (int i = 0; i < total; ++i) {
            const double t = static_cast<double>(i) / fs;
            l[static_cast<std::size_t>(i)] = static_cast<float>(0.45 * std::sin(kTwoPi * 310.0 * t));
            r[static_cast<std::size_t>(i)] = l[static_cast<std::size_t>(i)];
            sc[static_cast<std::size_t>(i)] = static_cast<float>(0.7 * std::sin(kTwoPi * 110.0 * t));
        }
        for (int pos = 0; pos < total; pos += 256)
            eng.process(params, {172.0, 0.0, false}, l.data() + pos, r.data() + pos, 256, sc.data() + pos,
                        sc.data() + pos);
        return l;
    };

    const auto base = renderFm(0.0f, pf::FmSource::input);
    for (auto src : {pf::FmSource::input, pf::FmSource::sidechain, pf::FmSource::pitchSine}) {
        const auto mod = renderFm(0.75f, src);
        double diffEnergy = 0.0;
        for (int i = total / 2; i < total; ++i) {
            REQUIRE(std::isfinite(mod[static_cast<std::size_t>(i)]));
            REQUIRE(std::abs(mod[static_cast<std::size_t>(i)]) < 3.5f);
            const double d = static_cast<double>(mod[static_cast<std::size_t>(i)] - base[static_cast<std::size_t>(i)]);
            diffEnergy += d * d;
        }
        CHECK(diffEnergy > 0.5);
    }
}

TEST_CASE("Mid and Side stereo routing cleanly isolate their respective components", "[engine]") {
    constexpr double fs = 48000.0;
    constexpr int total = 4096;

    // Pure Side input (L = +s, R = -s) into Mid routing: Side passes uncolored, Mid stays ~0.
    {
        pf::Engine eng;
        eng.prepare(fs, 256, 4);
        auto params = pf::Params::defaults();
        params[pf::ParamId::stereoMode] = static_cast<float>(pf::StereoMode::mid);
        params[pf::ParamId::subLock] = 0.0f;
        params[pf::ParamId::drive] = 0.8f;
        params[pf::ParamId::limit] = 0.0f;
        params[pf::ParamId::motion] = 0.0f;

        std::vector<float> l(total), r(total);
        for (int i = 0; i < total; ++i) {
            const float s = static_cast<float>(0.4 * std::sin(kTwoPi * 520.0 * static_cast<double>(i) / fs));
            l[static_cast<std::size_t>(i)] = s;
            r[static_cast<std::size_t>(i)] = -s;
        }
        for (int pos = 0; pos < total; pos += 256)
            eng.process(params, {172.0, 0.0, false}, l.data() + pos, r.data() + pos, 256);

        double midEnergy = 0.0, sideEnergy = 0.0;
        for (int i = total / 2; i < total; ++i) {
            const double m = 0.5 * (l[static_cast<std::size_t>(i)] + r[static_cast<std::size_t>(i)]);
            const double s = 0.5 * (l[static_cast<std::size_t>(i)] - r[static_cast<std::size_t>(i)]);
            midEnergy += m * m;
            sideEnergy += s * s;
        }
        CHECK(sideEnergy > 100.0);
        CHECK(midEnergy < 1e-6);
    }

    // Pure Mid input (L = +s, R = +s) into Side routing: Mid passes cleanly, Side stays ~0.
    {
        pf::Engine eng;
        eng.prepare(fs, 256, 4);
        auto params = pf::Params::defaults();
        params[pf::ParamId::stereoMode] = static_cast<float>(pf::StereoMode::side);
        params[pf::ParamId::subLock] = 0.0f;
        params[pf::ParamId::drive] = 0.8f;
        params[pf::ParamId::limit] = 0.0f;
        params[pf::ParamId::motion] = 0.0f;

        std::vector<float> l(total), r(total);
        for (int i = 0; i < total; ++i) {
            const float s = static_cast<float>(0.4 * std::sin(kTwoPi * 520.0 * static_cast<double>(i) / fs));
            l[static_cast<std::size_t>(i)] = s;
            r[static_cast<std::size_t>(i)] = s;
        }
        for (int pos = 0; pos < total; pos += 256)
            eng.process(params, {172.0, 0.0, false}, l.data() + pos, r.data() + pos, 256);

        double midEnergy = 0.0, sideEnergy = 0.0;
        for (int i = total / 2; i < total; ++i) {
            const double m = 0.5 * (l[static_cast<std::size_t>(i)] + r[static_cast<std::size_t>(i)]);
            const double s = 0.5 * (l[static_cast<std::size_t>(i)] - r[static_cast<std::size_t>(i)]);
            midEnergy += m * m;
            sideEnergy += s * s;
        }
        CHECK(midEnergy > 100.0);
        CHECK(sideEnergy < 1e-6);
    }
}

TEST_CASE("PDC latency includes oversampling HIIR group delay plus limiter lookahead", "[engine]") {
    pf::Engine eng2, eng4, eng8;
    eng2.prepare(48000.0, 256, 2);
    eng4.prepare(48000.0, 256, 4);
    eng8.prepare(48000.0, 256, 8);
    CHECK(eng2.oversamplingFactor() == 2);
    CHECK(eng4.oversamplingFactor() == 4);
    CHECK(eng8.oversamplingFactor() == 8);
    CHECK(eng4.latencySamples() == eng2.latencySamples() + 1);
    CHECK(eng8.latencySamples() == eng4.latencySamples() + 1);
}

TEST_CASE(
    "Free Hz lanes are BPM-independent, phase-locked across Follow+Ratio, retrigger on MIDI, and block-size exact",
    "[engine]") {
    constexpr double fs = 48000.0;
    constexpr int total = 4800; // 0.1 s

    auto makeFreeParams = [] {
        auto p = pf::Params::defaults();
        p[pf::ParamId::motion] = 0.85f;
        p[pf::ParamId::lane1On] = 1.0f;
        p[pf::ParamId::lane1Target] = static_cast<float>(pf::LaneTarget::morphX);
        p[pf::ParamId::lane1Mode] = static_cast<float>(pf::LaneMode::freeRun);
        p[pf::ParamId::lane1Hz] = 2.5f;
        p[pf::ParamId::lane1Depth] = 0.7f;
        p[pf::ParamId::lane2On] = 1.0f;
        p[pf::ParamId::lane2Target] = static_cast<float>(pf::LaneTarget::morphY);
        p[pf::ParamId::lane2Follow] = 1.0f; // Follow Lane 1
        p[pf::ParamId::lane2Ratio] = 4.0f;  // x2 -> 5.0 Hz
        p[pf::ParamId::lane2Mode] = static_cast<float>(pf::LaneMode::sync);
        p[pf::ParamId::lane2Depth] = 0.6f;
        p[pf::ParamId::lane3On] = 0.0f;
        p[pf::ParamId::lane4On] = 0.0f;
        return p;
    };

    // 1) BPM independence & linked phase-ratio accuracy.
    {
        const auto params = makeFreeParams();
        std::vector<float> lSlow(total), rSlow(total), lFast(total), rFast(total);
        for (int i = 0; i < total; ++i) {
            const float s = static_cast<float>(0.35 * std::sin(kTwoPi * 110.0 * static_cast<double>(i) / fs));
            lSlow[static_cast<std::size_t>(i)] = lFast[static_cast<std::size_t>(i)] = s;
            rSlow[static_cast<std::size_t>(i)] = rFast[static_cast<std::size_t>(i)] = s;
        }
        pf::Engine engSlow, engFast;
        engSlow.prepare(fs, 256, 4);
        engFast.prepare(fs, 256, 4);
        engSlow.process(params, {85.0, 1.25, true}, lSlow.data(), rSlow.data(), total);
        engFast.process(params, {178.0, 9.75, true}, lFast.data(), rFast.data(), total);

        double maxDiff = 0.0;
        for (int i = 0; i < total; ++i) {
            maxDiff = std::max(maxDiff, std::abs(static_cast<double>(lSlow[static_cast<std::size_t>(i)] -
                                                                     lFast[static_cast<std::size_t>(i)])));
            maxDiff = std::max(maxDiff, std::abs(static_cast<double>(rSlow[static_cast<std::size_t>(i)] -
                                                                     rFast[static_cast<std::size_t>(i)])));
        }
        CHECK(maxDiff == 0.0);

        // At sample (total - 1) = 4799 -> t = 4799 / 48000 s:
        // Lane 1 phase = frac(t * 2.5), Lane 2 phase = frac(t * 5.0 + 90/360)
        const double tEnd = static_cast<double>(total - 1) / fs;
        const auto phases = engSlow.lanePhases();
        CHECK(phases[0] == Catch::Approx(static_cast<float>(std::fmod(tEnd * 2.5, 1.0))).margin(1e-5f));
        CHECK(phases[1] == Catch::Approx(static_cast<float>(std::fmod(tEnd * 5.0 + 0.25, 1.0))).margin(1e-5f));
        CHECK(engSlow.freeSeconds() == Catch::Approx(tEnd).margin(1e-9));
    }

    // 2) MIDI noteOn retriggers Free R and 1-Shot lanes deterministically.
    {
        auto params = pf::Params::defaults();
        params[pf::ParamId::lane1On] = 1.0f;
        params[pf::ParamId::lane1Mode] = static_cast<float>(pf::LaneMode::freeRetrig);
        params[pf::ParamId::lane1Hz] = 4.0f;
        params[pf::ParamId::lane1Phase] = 0.0f;
        params[pf::ParamId::lane2On] = 1.0f;
        params[pf::ParamId::lane2Mode] = static_cast<float>(pf::LaneMode::oneShot);
        params[pf::ParamId::lane2Rate] = 9.0f; // 1/16 = 0.25 qn
        params[pf::ParamId::lane2Phase] = 0.0f;

        pf::Engine eng;
        eng.prepare(fs, 256, 4);
        std::vector<float> l(4800, 0.1f), r(4800, 0.1f);
        eng.process(params, {120.0, 0.0, true}, l.data(), r.data(), 4800);
        CHECK(eng.noteSeconds() > 0.09);
        CHECK(eng.lanePhases()[1] < 0.99999f);

        // Process long enough for 1-Shot (0.25 qn = 0.125 s at 120 BPM) to saturate.
        eng.process(params, {120.0, 0.2, true}, l.data(), r.data(), 4800);
        CHECK(eng.lanePhases()[1] > 0.999f);

        // Retrigger via MIDI noteOn -> resets noteSeconds and restarts both Free R and 1-Shot near 0!
        eng.noteOn(48);
        std::vector<float> tinyL(16, 0.1f), tinyR(16, 0.1f);
        eng.process(params, {120.0, 0.4, true}, tinyL.data(), tinyR.data(), 16);
        CHECK(eng.noteSeconds() == Catch::Approx(15.0 / fs).margin(1e-9));
        CHECK(eng.lanePhases()[0] < 0.01f);
        CHECK(eng.lanePhases()[1] < 0.01f);
    }

    // 3) Bit-exact block-size independence with Free and Free R lanes active.
    {
        auto params = makeFreeParams();
        params[pf::ParamId::lane3On] = 1.0f;
        params[pf::ParamId::lane3Mode] = static_cast<float>(pf::LaneMode::freeRetrig);
        params[pf::ParamId::lane3Hz] = 3.2f;
        params[pf::ParamId::lane3Depth] = 0.5f;

        auto renderChunked = [&](int chunkSize) {
            pf::Engine eng;
            eng.prepare(fs, 512, 4);
            std::vector<float> l(1024), r(1024);
            for (int i = 0; i < 1024; ++i) {
                l[static_cast<std::size_t>(i)] = static_cast<float>(0.4 * std::sin(kTwoPi * 165.0 * i / fs));
                r[static_cast<std::size_t>(i)] = static_cast<float>(0.4 * std::cos(kTwoPi * 165.0 * i / fs));
            }
            for (int pos = 0; pos < 1024; pos += chunkSize) {
                const int n = std::min(chunkSize, 1024 - pos);
                eng.process(params, {172.0, static_cast<double>(pos) * (172.0 / (60.0 * fs)), true}, l.data() + pos,
                            r.data() + pos, n);
            }
            return std::pair{l, r};
        };

        const auto [refL, refR] = renderChunked(512);
        for (int bs : {1, 7, 64, 257}) {
            const auto [curL, curR] = renderChunked(bs);
            for (std::size_t i = 0; i < refL.size(); ++i) {
                REQUIRE(curL[i] == refL[i]);
                REQUIRE(curR[i] == refR[i]);
            }
        }
    }
}
