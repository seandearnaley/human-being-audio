// SPDX-License-Identifier: GPL-3.0-or-later
#include "engine/Cutter.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

using namespace livecut::engine;

TEST_CASE("Livecut 2.0 engine is deterministic, block-size invariant, and bounded across all Classic procedures",
          "[cutter][perform][legacy]") {
    const auto procedure = GENERATE (0, 1, 2, 3);
    const auto blockSize = GENERATE (137, 512);
    Cutter a, b;
    constexpr double rate = 48000.0;
    a.prepare(rate);
    b.prepare(rate);
    a.setProcedure(static_cast<Procedure>(procedure));
    b.setProcedure(static_cast<Procedure>(procedure));
    CutParameters parameters;
    parameters.minPhraseBars = parameters.maxPhraseBars = 4;
    parameters.minRepeats = 1;
    parameters.maxRepeats = 4;
    parameters.minCents = -900.0f;
    parameters.maxCents = 700.0f;
    parameters.minPan = -0.7f;
    parameters.maxPan = 0.6f;
    parameters.minAmp = 0.3f;
    parameters.maxAmp = 0.9f;
    a.setParameters(parameters);
    b.setParameters(parameters);
    a.setFadeMilliseconds(2.5);
    b.setFadeMilliseconds(2.5);
    CrusherParameters crusher{true, 6.0f, 14.0f, 1700.0, 6000.0};
    CombParameters comb{true, CombParameters::Type::feedBack, 0.75f, 2.0f, 17.0f};
    a.setCrusher(crusher);
    b.setCrusher(crusher);
    a.setComb(comb);
    b.setComb(comb);

    constexpr int totalFrames = 48000 * 3;
    std::vector<float> leftA(static_cast<std::size_t>(totalFrames)), rightA(static_cast<std::size_t>(totalFrames));
    std::vector<float> leftB(static_cast<std::size_t>(totalFrames)), rightB(static_cast<std::size_t>(totalFrames));
    for (int i = 0; i < totalFrames; ++i) {
        leftA[static_cast<std::size_t>(i)] = leftB[static_cast<std::size_t>(i)] =
            std::sin(static_cast<float>(i) * 0.043f) * 0.8f;
        rightA[static_cast<std::size_t>(i)] = rightB[static_cast<std::size_t>(i)] =
            std::cos(static_cast<float>(i) * 0.057f) * 0.8f;
    }

    // Run A in `blockSize` chunks and B in uneven 61-sample chunks.
    double beats = 0.0;
    for (int done = 0; done < totalFrames;) {
        const int n = std::min(blockSize, totalFrames - done);
        const auto bar = static_cast<std::int64_t> (std::floor (beats / 4.0));
        const auto into = beats - static_cast<double> (bar) * 4.0;
        a.process(leftA.data() + done, rightA.data() + done, n, {172.0, 4.0}, {bar, into});
        done += n;
        beats += n * 172.0 / 60.0 / rate;
    }
    beats = 0.0;
    for (int done = 0; done < totalFrames;) {
        const int n = std::min(61, totalFrames - done);
        const auto bar = static_cast<std::int64_t>(std::floor(beats / 4.0));
        const auto into = beats - static_cast<double>(bar) * 4.0;
        b.process(leftB.data() + done, rightB.data() + done, n, {172.0, 4.0}, {bar, into});
        done += n;
        beats += n * 172.0 / 60.0 / rate;
    }

    REQUIRE(a.error() == Cutter::Error::none);
    REQUIRE(b.error() == Cutter::Error::none);
    float maxDiff = 0.0f;
    for (std::size_t i = 0; i < leftA.size(); ++i) {
        REQUIRE(std::isfinite(leftA[i]));
        REQUIRE(std::isfinite(rightA[i]));
        REQUIRE(std::abs(leftA[i]) <= 1.05f);
        REQUIRE(std::abs(rightA[i]) <= 1.05f);
        maxDiff = std::max(maxDiff, std::max(std::abs(leftA[i] - leftB[i]), std::abs(rightA[i] - rightB[i])));
    }
    CHECK(maxDiff < 1.0e-5f);
}
