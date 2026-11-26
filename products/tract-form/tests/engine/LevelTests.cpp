// Growl as a level, and the sub/growl split following the note (0.2, after
// the preset designer's measurements): growlLevel is applied after the growl's
// ceiling stage, and the split is subSplit x f0.
#include "helpers.h"

#include "tf/render.h"
#include "tf/voice_pitch.h"

#include <hba/dsp/control.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace tf;
using namespace tf::test;
using analysis::Signal;

namespace {

Params lanesOff(Params p) {
    for (int i = 0; i < kLaneCount; ++i) p[laneParam(i, LaneField::on)] = 0.0f;
    return p;
}

struct Measured {
    analysis::Metrics metrics;
    RenderStats stats;
};

// One held note, 1.5 s at 2x, measured as tf-render --metrics does (the sub
// band and the growl split at subSplit x f0).
Measured measure(const Params& params, int note) {
    RenderRequest request;
    request.params = params;
    request.note = note;
    request.bars = 1.0;
    request.oversampling = 2;
    request.tailSeconds = 0.0;
    Measured m;
    const auto audio = analysis::deinterleave(renderOffline(request, &m.stats));
    analysis::MetricsRequest q;
    q.sampleRate = request.sampleRate;
    q.subFreq = splitHz(params, note);
    q.xover = params[ParamId::xover];
    q.steadyBegin = static_cast<std::size_t>(0.3 * request.sampleRate);
    q.steadyEnd = static_cast<std::size_t>(m.stats.heldSamples);
    q.f0 = noteHz(note);
    q.tiltUpperHz = 0.8 * params[ParamId::reach] * q.f0;
    m.metrics = analysis::computeMetrics(audio, q);
    return m;
}

}

// ---- Growl is a level ------------------------------------------------------------------

TEST_CASE("growl level sets loudness and nothing else", "[engine][level]") {
    // growlLevel is a linear gain after the clipper, high-passes and limiter:
    // 0.25 -> 0.5 -> 1 is +6.02 dB a step, the clipper works the same at
    // every setting, and the growl's spectrum (tilt) does not move.
    const bool hot = GENERATE(false, true);
    CAPTURE(hot);
    Params p = lanesOff(hot ? hotPatch() : Params::defaults());
    std::vector<Measured> runs;
    for (float level : { 0.25f, 0.5f, 1.0f }) {
        p[ParamId::growlLevel] = level;
        runs.push_back(measure(p, 29));
        const auto& m = runs.back();
        std::printf("[growl level] %s, F1, growl %.2f: growl %.2f dB, growl - sub %.2f dB, tilt %.2f dB/oct, clipper %.2f dB "
                    "mean\n", hot ? "hot" : "default", static_cast<double>(level), m.metrics.growlRmsDb,
                    m.metrics.growlRmsDb - m.metrics.subRmsDb, m.metrics.tiltDbPerOct.value_or(0.0),
                    static_cast<double>(m.stats.meanGrowlClipDb));
    }
    const double step = 20.0 * std::log10(2.0);
    double tiltLo = 1e9, tiltHi = -1e9, clipLo = 1e9, clipHi = -1e9;
    for (std::size_t i = 0; i < runs.size(); ++i) {
        if (i > 0) CHECK(std::abs(runs[i].metrics.growlRmsDb - runs[i - 1].metrics.growlRmsDb - step) <= 0.5);
        REQUIRE(runs[i].metrics.tiltDbPerOct.has_value());
        tiltLo = std::min(tiltLo, *runs[i].metrics.tiltDbPerOct);
        tiltHi = std::max(tiltHi, *runs[i].metrics.tiltDbPerOct);
        clipLo = std::min(clipLo, static_cast<double>(runs[i].stats.meanGrowlClipDb));
        clipHi = std::max(clipHi, static_cast<double>(runs[i].stats.meanGrowlClipDb));
    }
    CHECK(tiltHi - tiltLo < 0.5);
    CHECK(clipHi - clipLo <= 0.2);
    if (hot) {
        // The hot patch drives the clipper 1.5-5 dB at any Growl.
        CHECK(clipLo >= 1.5);
        CHECK(clipHi <= 5.0);
    }
}

// ---- The split follows the note --------------------------------------------------------

TEST_CASE("the sub/growl balance holds across a bassline's range", "[engine][level]") {
    // E1, F1, A1, E2: the split moves with the note, so growl - sub stays
    // within 1.5 dB (a fixed 120 Hz split moved it 3-4 dB from E1 to A1 at
    // low Reach) and the sub's level within 0.2 dB.
    const float reach = GENERATE(96.0f, 16.0f);
    CAPTURE(reach);
    Params p = lanesOff(Params::defaults());
    p[ParamId::reach] = reach;
    double balanceLo = 1e9, balanceHi = -1e9, subLo = 1e9, subHi = -1e9;
    std::printf("[split] reach %.0f, growl - sub:", static_cast<double>(reach));
    for (int note : { 28, 29, 33, 40 }) {
        const auto m = measure(p, note).metrics;
        const double balance = m.growlRmsDb - m.subRmsDb;
        std::printf(" note %d %.2f dB (sub %.2f)", note, balance, m.subRmsDb);
        balanceLo = std::min(balanceLo, balance);
        balanceHi = std::max(balanceHi, balance);
        subLo = std::min(subLo, m.subRmsDb);
        subHi = std::max(subHi, m.subRmsDb);
    }
    std::printf("\n");
    CHECK(balanceHi - balanceLo <= 1.5);
    CHECK(subHi - subLo <= 0.2);
}

TEST_CASE("the split follows a glide without clicks", "[engine][level]") {
    // E1 -> A1 legato over 100 ms. The output's largest sample-to-sample step
    // stays within what the two notes reach played separately.
    Setup s;
    s.params = lanesOff(Params::defaults());
    s.params[ParamId::glide] = 100.0f;
    s.oversampling = 2;
    const double fs = s.sampleRate;
    const auto maxStep = [&](const Signal& x, double from) {
        double m = 0.0;
        for (auto i = static_cast<std::size_t>(from * fs); i + 1 < x.size(); ++i) m = std::max(m, std::abs(x[i + 1] - x[i]));
        return m;
    };
    const double low = maxStep(render(s, { { 0, 28, true } }, frames(0.8, fs)).left, 0.3);
    const double high = maxStep(render(s, { { 0, 33, true } }, frames(0.8, fs)).left, 0.3);
    const auto glide = render(s, { { 0, 28, true }, { frames(0.4, fs), 33, true } }, frames(0.9, fs));
    const double step = maxStep(glide.left, 0.3);
    std::printf("[split] glide E1 -> A1 in 100 ms: largest step %.4f; E1 alone %.4f, A1 alone %.4f\n", step, low, high);
    CHECK(step <= 1.1 * std::max(low, high));
}
