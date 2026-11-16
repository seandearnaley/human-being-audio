// Real-time factor of the default patch: 48 kHz, 4x, block 64.
#include "helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdio>

using namespace tf;

TEST_CASE("benchmark: the default patch runs well faster than real time", "[engine][benchmark]") {
    constexpr double fs = 48000.0;
    constexpr int block = 64;
    for (int factor : { 4, 16 }) {
        Engine e;
        e.prepare(fs, block, factor);
        const Params params = Params::defaults();
        Transport t;
        t.playing = true;
        std::vector<float> l(block), r(block);
        e.noteOn(29);
        const long long blocks = static_cast<long long>(10.0 * fs) / block;  // 10 s of audio
        const auto start = std::chrono::steady_clock::now();
        for (long long b = 0; b < blocks; ++b) {
            t.ppq = static_cast<double>(b * block) * t.bpm / 60.0 / fs;
            e.process(params, t, l.data(), r.data(), block);
        }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        const double factorRt = static_cast<double>(blocks * block) / fs / seconds;
        std::printf("[benchmark] default patch, 48 kHz, %dx, block 64: %.1fx real time (%.2f%% of one core)\n", factor,
                    factorRt, 100.0 / factorRt);
        if (factor == 4) CHECK(factorRt > 5.0);
    }
}
