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

    double maxDiff = 0.0;
    for (int i = 0; i < total; ++i) {
        REQUIRE(std::isfinite(modL[static_cast<std::size_t>(i)]));
        REQUIRE(std::isfinite(modR[static_cast<std::size_t>(i)]));
        maxDiff = std::max(maxDiff, static_cast<double>(std::abs(modL[static_cast<std::size_t>(i)] -
                                                                 idleL[static_cast<std::size_t>(i)])));
    }
    CHECK(maxDiff > 1e-2);
}
