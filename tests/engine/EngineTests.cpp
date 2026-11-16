// Engine-level properties: one test (or more) per bullet of the spec's
// "Verification" section, plus note handling, latency and taps.
#include "helpers.h"

#include "crossover.h"
#include "limiter.h"
#include "squash.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <cmath>
#include <complex>
#include <cstdio>
#include <random>

using namespace tf;
using namespace tf::test;
using analysis::Signal;

namespace {

std::vector<NoteEvent> held(int note) { return { NoteEvent { 0, note, true } }; }

Params fullDepthLanes(Params p) {
    p[ParamId::motion] = 1.0f;
    for (int i = 0; i < kLaneCount; ++i) {
        p[laneParam(i, LaneField::on)] = 1.0f;
        p[laneParam(i, LaneField::depth)] = 1.0f;
    }
    return p;
}

double semitones(double hz, double ref) { return 12.0 * std::log2(hz / ref); }

}

// ---- Verification: sub stability -------------------------------------------

TEST_CASE("sub stability: the low-passed output varies < 0.5 dB with every lane at full depth", "[engine][spec]") {
    const int note = GENERATE(24, 29, 35);
    const float depthSign = GENERATE(1.0f, -1.0f);
    CAPTURE(note, depthSign);
    Setup s;
    s.params = fullDepthLanes(Params::defaults());
    for (int i = 0; i < kLaneCount; ++i) s.params[laneParam(i, LaneField::depth)] = depthSign;
    const double fs = s.sampleRate;
    const auto out = render(s, held(note), frames(4.0, fs));
    const Signal sub = analysis::lr4Lowpass(mid(out), s.params[ParamId::subFreq], fs);
    const Signal w = analysis::windowedRmsDb(sub, static_cast<std::size_t>(0.5 * fs), sub.size(), fs);
    const double range = *std::max_element(w.begin(), w.end()) - *std::min_element(w.begin(), w.end());
    INFO("50 ms sub RMS range " << range << " dB");
    std::printf("[sub stability] note %d depth %+.0f: range %.3f dB\n", note, static_cast<double>(depthSign), range);
    CHECK(range < 0.5);
}

// ---- Verification: mono low end -------------------------------------------

// The side is high-passed with LR4 at the crossover (spec), which is only 6 dB
// down at the crossover and 24 dB an octave below it, so side energy in the
// whole band below the crossover cannot be 60 dB under the mid: reese
// harmonics just under the crossover leak at -30..-45 dB. The test therefore
// measures the side an octave below the crossover, where the LR4 has done its
// work, against the mid in the whole band below the crossover, and prints the
// literal full-band figure for reference.
TEST_CASE("mono low end: side energy an octave below the crossover is 60 dB under the mid", "[engine][spec]") {
    const int note = GENERATE(24, 29, 36, 41);
    const float width = GENERATE(0.64f, 1.0f);
    const float growl = GENERATE(0.76f, 1.0f);
    CAPTURE(note, width, growl);
    Setup s;
    s.params[ParamId::width] = width;
    s.params[ParamId::growlLevel] = growl;
    const double fs = s.sampleRate;
    const double xover = s.params[ParamId::xover];
    const auto out = render(s, held(note), frames(3.0, fs));
    const auto from = static_cast<std::size_t>(0.5 * fs);
    const Signal m = slice(mid(out), from, out.left.size()), sd = slice(side(out), from, out.left.size());
    constexpr std::size_t frame = 1 << 15;
    const auto pm = analysis::welch(m, frame), ps = analysis::welch(sd, frame);
    double midBelow = 0, sideBelow = 0, sideOctaveBelow = 0;
    for (std::size_t i = 1; i < pm.size(); ++i) {
        const double hz = static_cast<double>(i) * fs / frame;
        if (hz >= xover) break;
        midBelow += pm[i];
        sideBelow += ps[i];
        if (hz < xover / 2) sideOctaveBelow += ps[i];
    }
    const double octave = 10 * std::log10(sideOctaveBelow / midBelow);
    const double full = 10 * std::log10(sideBelow / midBelow);
    std::printf("[mono low end] note %d width %.2f growl %.2f: side < xover/2 %.1f dB, side < xover %.1f dB (re mid < xover)\n",
                note, static_cast<double>(width), static_cast<double>(growl), octave, full);
    CHECK(octave < -60.0);
}

// ---- Verification: block independence -------------------------------------

TEST_CASE("block independence: block sizes 1, 7, 64 and 512 give identical output", "[engine][spec]") {
    // 3584 = 7 * 512: events on multiples of it fall on block boundaries for every size.
    const std::vector<NoteEvent> events { { 0, 29, true }, { 3584 * 4, 36, true }, { 3584 * 6, 36, false },
                                          { 3584 * 8, 29, false }, { 3584 * 9, 31, true } };
    const long long count = 3584 * 12;
    Setup s;
    s.block = 64;
    const auto reference = render(s, events, count);
    for (int block : { 1, 7, 512 }) {
        CAPTURE(block);
        s.block = block;
        const auto out = render(s, events, count);
        CHECK(maxAbsDiff(out.left, reference.left) == 0.0);
        CHECK(maxAbsDiff(out.right, reference.right) == 0.0);
        CHECK(maxAbsDiff(out.master, reference.master) == 0.0);
        CHECK(maxAbsDiff(out.growl, reference.growl) == 0.0);
    }
}

// ---- Verification: ceiling --------------------------------------------------

TEST_CASE("ceiling: the 4x true peak never exceeds the ceiling with limit on", "[engine][spec]") {
    const int seed = GENERATE(range(0, 40));
    CAPTURE(seed);
    std::mt19937 rng(static_cast<unsigned>(seed) * 7919u + 11u);
    Setup s;
    s.params = randomParams(rng);
    s.params[ParamId::limit] = 1.0f;
    s.params[ParamId::growlLevel] = std::max(0.6f, s.params[ParamId::growlLevel]);  // make it work
    s.params[ParamId::subLevel] = std::max(0.6f, s.params[ParamId::subLevel]);
    s.oversampling = 2;
    s.block = 128;
    const int note = std::uniform_int_distribution<int>(20, 60)(rng);
    // Released before the end, so the render ends in silence: cutting a signal
    // off mid-wave would make the reference interpolator ring at the edge.
    const auto out = render(s, { { 0, note, true }, { frames(0.6, s.sampleRate), note, false } }, frames(0.8, s.sampleRate));
    const double ceiling = s.params[ParamId::ceiling];
    const double tp = analysis::toDb(std::max(analysis::truePeak4x(out.left), analysis::truePeak4x(out.right)));
    INFO("true peak " << tp << " dBFS, ceiling " << ceiling << " dB");
    CHECK(tp <= ceiling + 0.1);
}

// ---- Verification: aliasing ---------------------------------------------------

TEST_CASE("aliasing: inharmonic energy is 70 dB under the harmonics at 4x", "[engine][spec]") {
    // A 1 kHz note at 44.1 kHz (aliases land between the harmonics), drive and
    // coupling at 1, detune 0. Feedback and lanes off so the note is periodic.
    const auto type = GENERATE(0, 1, 2);
    CAPTURE(type);
    Setup s;
    s.sampleRate = 44100.0;
    s.oversampling = 4;
    s.params = staticPatch();
    s.params[ParamId::drive] = 1.0f;
    s.params[ParamId::coupling] = 1.0f;
    s.params[ParamId::aDetune] = 0.0f;
    s.params[ParamId::bDetune] = 0.0f;
    s.params[ParamId::fbAmount] = 0.0f;
    s.params[ParamId::foldType] = static_cast<float>(type);
    s.bend = static_cast<float>(semitones(1000.0, noteHz(83)));
    const auto out = render(s, held(83), frames(2.0, s.sampleRate));

    // Measured over the audio band, 20 Hz - 20 kHz. Between 20 kHz and Nyquist
    // every half-band decimator lets images through its transition band (it is
    // -6 dB at Nyquist by construction), so that region is reported, not tested.
    constexpr std::size_t n = 1 << 16;
    const Signal x = slice(mid(out), out.left.size() - n, out.left.size());
    const auto p = analysis::powerSpectrum(x.data(), n, analysis::blackmanHarris(n));
    const double binHz = s.sampleRate / n;
    double harmonic = 0, inharmonic = 0, above = 0;
    for (std::size_t i = 1; i < p.size(); ++i) {
        const double hz = static_cast<double>(i) * binHz;
        if (hz < 20.0) continue;
        const double k = std::round(hz / 1000.0);
        const bool isHarmonic = k >= 1 && std::abs(hz - 1000.0 * k) <= 8 * binHz;
        if (isHarmonic) harmonic += p[i];
        else if (hz <= 20000.0) inharmonic += p[i];
        else above += p[i];
    }
    const double ratio = 10.0 * std::log10(inharmonic / harmonic);
    std::printf("[aliasing] fold type %d: inharmonic %.1f dB below 20 kHz, %.1f dB including 20 kHz-Nyquist\n", type,
                ratio, 10.0 * std::log10((inharmonic + above) / harmonic));
    CHECK(ratio <= -70.0);
}

// ---- Verification: path alignment -----------------------------------------------

TEST_CASE("path alignment: the sub and growl paths have equal latency to the sample", "[engine][spec]") {
    // A patch where both reese voices are exactly the master sine: the growl tap
    // must then equal the master tap through the growl path's (linear) filters.
    const int factor = GENERATE(2, 4, 8, 16);
    const int note = GENERATE(45, 69, 93);
    CAPTURE(factor, note);
    Setup s;
    s.oversampling = factor;
    s.params = staticPatch();
    s.params[ParamId::coupling] = 0.0f;
    s.params[ParamId::aDetune] = s.params[ParamId::bDetune] = 0.0f;
    s.params[ParamId::aPhase] = s.params[ParamId::bPhase] = 0.0f;
    s.params[ParamId::fbAmount] = 0.0f;
    s.params[ParamId::width] = 0.0f;
    s.params[ParamId::tract] = 0.0f;
    s.params[ParamId::warp] = 0.0f;
    s.params[ParamId::squash] = 0.0f;
    s.params[ParamId::growlLevel] = 1.0f;
    s.params[ParamId::midGain] = 0.0f;
    const double fs = s.sampleRate;
    const auto out = render(s, held(note), frames(0.5, fs));

    const auto cSub = dsp::butterworth(s.params[ParamId::subFreq], fs);
    const auto cX = dsp::butterworth(s.params[ParamId::xover], fs);
    const auto cLo = dsp::butterworth(dsp::kSquashLowHz, fs);
    const auto cHi = dsp::butterworth(dsp::kSquashHighHz, fs);
    dsp::Lr4Highpass hp;
    dsp::ThreeBandSplit split;
    dsp::Lr4Allpass ap;
    Signal expected(out.master.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        double lo, mi, hi;
        split.process(cLo, cHi, hp.process(cSub, out.master[i]), lo, mi, hi);
        expected[i] = ap.process(cX, lo + mi + hi);
    }
    // Compare once the taps run (they are silent before the latency). The
    // growl path's fixed gain staging is fitted out (least squares).
    const auto from = static_cast<std::size_t>(out.latency + 64);
    const Signal a = slice(out.growl, from, out.growl.size());
    const auto residual = [&](std::size_t offset) {
        const Signal b = slice(expected, from - offset, expected.size() - offset);
        double ab = 0, bb = 0;
        for (std::size_t i = 0; i < a.size(); ++i) {
            ab += a[i] * b[i];
            bb += b[i] * b[i];
        }
        const double gain = ab / bb;
        double worst = 0;
        for (std::size_t i = 0; i < a.size(); ++i) worst = std::max(worst, std::abs(a[i] - gain * b[i]));
        return worst / (std::abs(gain) * maxAbs(b));
    };
    const double err = residual(0);
    INFO("relative error " << err);
    CHECK(err < 1e-4);
    // A one-sample offset would be glaring: check the test can see it.
    CHECK(residual(1) > 1e-2);
}

TEST_CASE("the master tap is latency-aligned with the output", "[engine]") {
    Setup s;
    s.params = staticPatch();
    s.params[ParamId::growlLevel] = 0.0f;
    s.params[ParamId::subLevel] = 1.0f;
    s.params[ParamId::limit] = 0.0f;
    const double fs = s.sampleRate;
    const auto out = render(s, held(33), frames(0.5, fs));
    const auto c = dsp::butterworth(s.params[ParamId::subFreq], fs);
    dsp::Lr4Lowpass lp;
    double worst = 0.0;
    for (std::size_t i = 0; i < out.master.size(); ++i) {
        const double expected = dsp::softClip(lp.process(c, out.master[i]));
        if (i > static_cast<std::size_t>(out.latency) + 200) worst = std::max(worst, std::abs(expected - out.left[i]));
    }
    CHECK(worst < 1e-5);
}

// ---- Verification: fuzz ------------------------------------------------------------

TEST_CASE("fuzz: random parameters and note streams give finite, bounded output", "[engine][spec]") {
    std::mt19937 rng(20261115);
    double worstPeak = 0.0;
    for (int run = 0; run < 240; ++run) {
        Setup s;
        s.params = randomParams(rng);
        s.oversampling = std::uniform_int_distribution<int>(0, 2)(rng) == 0 ? 2 : 4;
        s.block = std::uniform_int_distribution<int>(1, 300)(rng);
        s.bpm = std::uniform_real_distribution<double>(60.0, 200.0)(rng);
        s.playing = std::uniform_int_distribution<int>(0, 1)(rng) == 1;
        s.ppqStart = std::uniform_real_distribution<double>(-4.0, 100.0)(rng);
        const long long count = frames(0.15, s.sampleRate);
        std::vector<NoteEvent> events;
        long long t = 0;
        while (true) {
            t += std::uniform_int_distribution<long long>(0, 1500)(rng);
            if (t >= count) break;
            NoteEvent e;
            e.frame = t;
            const int kind = std::uniform_int_distribution<int>(0, 5)(rng);
            e.note = std::uniform_int_distribution<int>(0, 127)(rng);
            e.on = kind < 3;
            if (kind == 5) {
                e.bendOnly = true;
                e.bend = std::uniform_real_distribution<float>(-3.0f, 3.0f)(rng);
            }
            events.push_back(e);
        }
        const auto out = render(s, events, count);
        INFO("run " << run);
        REQUIRE(allFinite(out.left));
        REQUIRE(allFinite(out.right));
        REQUIRE(allFinite(out.master));
        REQUIRE(allFinite(out.growl));
        const double peak = std::max(maxAbs(out.left), maxAbs(out.right));
        worstPeak = std::max(worstPeak, peak);
        if (s.params.on(ParamId::limit)) REQUIRE(peak <= 1.0);
    }
    std::printf("[fuzz] 240 runs, worst sample peak %.4f\n", worstPeak);
}

// ---- Verification: determinism ----------------------------------------------------------

TEST_CASE("determinism: the same input twice gives identical output", "[engine][spec]") {
    const std::vector<NoteEvent> events { { 0, 29, true }, { 9000, 33, true }, { 15000, 33, false }, { 30000, 29, false } };
    Setup s;
    const auto a = render(s, events, 40000);
    const auto b = render(s, events, 40000);
    CHECK(maxAbsDiff(a.left, b.left) == 0.0);
    CHECK(maxAbsDiff(a.right, b.right) == 0.0);

    // And the same engine after reset().
    Engine e;
    e.prepare(s.sampleRate, s.block, s.oversampling);
    const auto c = render(e, s, events, 40000);
    e.reset();
    const auto d = render(e, s, events, 40000);
    CHECK(maxAbsDiff(c.left, a.left) == 0.0);
    CHECK(maxAbsDiff(d.left, a.left) == 0.0);
    CHECK(maxAbsDiff(d.right, a.right) == 0.0);
}

// ---- Notes, timing, latency -----------------------------------------------------------------

TEST_CASE("latency equals latencySamples()", "[engine]") {
    const int factor = GENERATE(2, 4, 8, 16);
    const double fs = GENERATE(44100.0, 48000.0);
    CAPTURE(factor, fs);
    Setup s;
    s.sampleRate = fs;
    s.oversampling = factor;
    s.params = staticPatch();
    const int note = 43;  // ~98 Hz: one period is far longer than the latency
    const auto out = render(s, held(note), frames(0.5, fs));

    // The master oscillator starts at phase 0 on the note-on (frame 0) and its
    // first oversampled sample is one increment in, so in base-rate time it is
    // sin(w (n + 1/factor)). The tap shows sin(w (n + 1/factor - latency)).
    const double w = 2.0 * dsp::kPi * noteHz(note) / fs;
    std::complex<double> acc { 0, 0 };
    const auto from = static_cast<std::size_t>(0.2 * fs);
    const double available = static_cast<double>(out.master.size() - from);
    const double periods = std::floor(available * w / (2 * dsp::kPi));
    const auto to = from + static_cast<std::size_t>(std::floor(periods * 2 * dsp::kPi / w));
    for (std::size_t n = from; n < to; ++n) acc += out.master[n] * std::polar(1.0, -w * static_cast<double>(n));
    // sin(w n + phi) correlates to (1/2j) e^{j phi}: phi = arg(acc) + pi/2.
    const double phi = std::arg(acc) + dsp::kPi / 2;
    double measured = (w / factor - phi) / w;
    const double period = 2 * dsp::kPi / w;
    while (measured < 0) measured += period;
    while (measured >= period) measured -= period;
    std::printf("[latency] %.1f kHz %2dx: reported %d, measured %.2f samples\n", fs / 1000, factor, out.latency, measured);
    CHECK(std::abs(measured - out.latency) <= 0.5);
}

TEST_CASE("oversampling is reduced at high host rates but stays at least 2x", "[engine]") {
    Engine e;
    e.prepare(44100.0, 64, 16);
    CHECK(e.oversamplingFactor() == 16);
    e.prepare(96000.0, 64, 16);
    CHECK(e.oversamplingFactor() == 8);
    e.prepare(192000.0, 64, 16);
    CHECK(e.oversamplingFactor() == 4);
    e.prepare(192000.0, 64, 4);
    CHECK(e.oversamplingFactor() == 2);
    e.prepare(96000.0, 64, 2);
    CHECK(e.oversamplingFactor() == 2);
    e.prepare(48000.0, 64, 1);
    CHECK(e.oversamplingFactor() == 1);
}

TEST_CASE("glide is linear in pitch and takes the glide time", "[engine]") {
    Setup s;
    s.params = staticPatch();
    s.params[ParamId::glide] = 100.0f;
    s.block = 48;  // 1 ms
    Engine e;
    e.prepare(s.sampleRate, s.block, s.oversampling);
    render(e, s, held(40), 9600);
    CHECK(semitones(e.currentHz(), noteHz(40)) == Catch::Approx(0.0).margin(1e-4));
    render(e, s, held(52), 2400);  // legato, 50 ms
    CHECK(semitones(e.currentHz(), noteHz(40)) == Catch::Approx(6.0).margin(0.1));
    render(e, s, {}, 2400);  // 100 ms
    CHECK(semitones(e.currentHz(), noteHz(52)) == Catch::Approx(0.0).margin(0.1));
    render(e, s, {}, 480);
    CHECK(semitones(e.currentHz(), noteHz(52)) == Catch::Approx(0.0).margin(1e-4));
}

TEST_CASE("a note from silence starts at pitch, without glide", "[engine]") {
    Setup s;
    s.params = staticPatch();
    s.params[ParamId::glide] = 300.0f;
    s.block = 48;
    Engine e;
    e.prepare(s.sampleRate, s.block, s.oversampling);
    render(e, s, held(40), 4800);
    render(e, s, { NoteEvent { 0, 40, false } }, 48000);  // released and decayed
    render(e, s, held(52), 48);
    CHECK(semitones(e.currentHz(), noteHz(52)) == Catch::Approx(0.0).margin(1e-4));
}

TEST_CASE("last-note priority", "[engine]") {
    Setup s;
    s.params = staticPatch();
    s.params[ParamId::glide] = 0.0f;
    s.block = 48;
    Engine e;
    e.prepare(s.sampleRate, s.block, s.oversampling);
    const auto at = [&](std::vector<NoteEvent> ev) {
        render(e, s, ev, 480);
        return 12.0 * std::log2(e.currentHz() / 440.0) + 69.0;
    };
    CHECK(at({ { 0, 40, true } }) == Catch::Approx(40.0).margin(1e-3));
    CHECK(at({ { 0, 45, true } }) == Catch::Approx(45.0).margin(1e-3));
    CHECK(at({ { 0, 43, true } }) == Catch::Approx(43.0).margin(1e-3));
    CHECK(at({ { 0, 43, false } }) == Catch::Approx(45.0).margin(1e-3));
    CHECK(at({ { 0, 40, false } }) == Catch::Approx(45.0).margin(1e-3));
    CHECK(e.gateOpen());
    at({ { 0, 45, false } });
    CHECK_FALSE(e.gateOpen());
}

TEST_CASE("pitch bend spans +-2 semitones", "[engine]") {
    Setup s;
    s.params = staticPatch();
    Engine e;
    e.prepare(s.sampleRate, s.block, s.oversampling);
    render(e, s, held(45), 4800);
    s.bend = 5.0f;
    render(e, s, {}, 4800);
    CHECK(semitones(e.currentHz(), noteHz(45)) == Catch::Approx(2.0).margin(1e-3));
    s.bend = -1.0f;
    render(e, s, {}, 4800);
    CHECK(semitones(e.currentHz(), noteHz(45)) == Catch::Approx(-1.0).margin(1e-3));
}

TEST_CASE("phase reset: two note-ons from silence produce identical attacks", "[engine]") {
    Setup s;
    s.playing = false;  // lanes run from the note-on
    Engine e;
    e.prepare(s.sampleRate, s.block, s.oversampling);
    const auto first = render(e, s, held(29), 9600);
    render(e, s, { { 0, 29, false } }, 48000);
    s.params[ParamId::coupling] = 0.1f;  // changed while silent...
    render(e, s, {}, 4800);
    s.params[ParamId::coupling] = Params::defaults()[ParamId::coupling];  // ...and back
    const auto second = render(e, s, held(29), 9600);
    CHECK(maxAbsDiff(first.left, second.left) == 0.0);
    CHECK(maxAbsDiff(first.right, second.right) == 0.0);
    CHECK(maxAbs(first.left) > 0.05);
}

TEST_CASE("phase reset while the gate is open fades out first", "[engine]") {
    Setup s;
    s.params = staticPatch();
    s.block = 16;
    const double fs = s.sampleRate;
    const long long retrigger = 9600 + 240;  // 5 ms into the release
    const auto out = render(s, { { 0, 29, true }, { 9600, 29, false }, { retrigger, 33, true } }, 14400);
    // Short-window level around the reset point (fade ~0.83 ms after the retrigger).
    const auto rmsAt = [&](long long centre) {
        const auto c = static_cast<std::size_t>(centre + out.latency);
        return analysis::toDb(analysis::rms(mid(out).data() + c - 4, 8));
    };
    const double before = rmsAt(retrigger - 48);
    double quietest = 0.0;
    for (long long t = retrigger; t < retrigger + 96; ++t) quietest = std::min(quietest, rmsAt(t) - before);
    INFO("dip " << quietest << " dB");
    CHECK(quietest < -30.0);
    (void)fs;
}

TEST_CASE("after the release the engine goes idle and writes exact silence", "[engine]") {
    Setup s;
    const auto out = render(s, { { 0, 29, true }, { 4800, 29, false } }, 48000);
    CHECK(maxAbs(slice(out.left, 4800 + 2400, 48000)) == 0.0);
    CHECK(maxAbs(slice(out.master, 4800 + 2400, 48000)) == 0.0);
    CHECK(maxAbs(slice(out.left, 0, 4800)) > 0.05);
}

TEST_CASE("width 0 is mono", "[engine]") {
    Setup s;
    s.params[ParamId::width] = 0.0f;
    const auto out = render(s, held(29), 24000);
    CHECK(maxAbsDiff(out.left, out.right) == 0.0);
    CHECK(maxAbs(out.left) > 0.05);
}

TEST_CASE("limit on and off give the same timing and, below the ceiling, the same signal", "[engine]") {
    Setup s;
    s.params = staticPatch();
    s.params[ParamId::growlLevel] = 0.2f;
    s.params[ParamId::subLevel] = 0.2f;
    const auto on = render(s, held(29), 24000);
    s.params[ParamId::limit] = 0.0f;
    const auto off = render(s, held(29), 24000);
    CHECK(maxAbsDiff(on.left, off.left) == 0.0);
    CHECK(maxAbs(on.left) > 0.05);
}

TEST_CASE("squash keeps the default patch's loudness within 2 dB", "[engine]") {
    Setup s;
    const double fs = s.sampleRate;
    std::vector<double> levels, growl;
    for (float amount : { 0.0f, 0.3f, 1.0f }) {
        s.params[ParamId::squash] = amount;
        const auto out = render(s, held(29), frames(3.0, fs));
        const auto from = static_cast<std::size_t>(0.5 * fs);
        levels.push_back(analysis::rmsDb(slice(mid(out), from, out.left.size())));
        growl.push_back(analysis::rmsDb(slice(out.growl, from, out.growl.size())));
        std::printf("[squash] amount %.1f: output %.2f dB RMS, growl %.2f dB RMS\n", static_cast<double>(amount),
                    levels.back(), growl.back());
    }
    CHECK(std::abs(levels[2] - levels[0]) < 2.0);
    CHECK(std::abs(levels[1] - levels[0]) < 2.0);
    // The sub dominates the output's RMS, so hold the growl alone to it too.
    CHECK(std::abs(growl[2] - growl[0]) < 2.0);
    CHECK(std::abs(growl[1] - growl[0]) < 2.0);
}

// ---- Lanes ---------------------------------------------------------------------------------

TEST_CASE("lanes lock to the host position", "[engine][lanes]") {
    // Every lane length divides 8 bars, so starting 8 bars later sounds the same.
    Setup s;
    const auto a = render(s, held(29), 24000);
    s.ppqStart = 32.0;
    const auto b = render(s, held(29), 24000);
    CHECK(maxAbsDiff(a.left, b.left) < 1e-5);
    s.ppqStart = 1.0;  // a quarter later: different movement
    const auto c = render(s, held(29), 24000);
    CHECK(maxAbsDiff(a.left, c.left) > 1e-3);
}

TEST_CASE("Steps repeats identically on every pass", "[engine][lanes]") {
    Setup s;
    s.bpm = 120.0;  // one bar = 96000 frames = 1500 blocks of 64
    s.params[laneParam(1, LaneField::shape)] = static_cast<float>(LaneShape::steps);
    s.params[laneParam(1, LaneField::rate)] = 3.0f;  // 1 bar
    s.params[ParamId::motion] = 1.0f;
    const auto out = render(s, held(29), 96000 * 3);
    REQUIRE(out.laneValues.size() == 4500);
    double worst = 0, spread = 0;
    for (std::size_t i = 0; i < 1500; ++i) {
        worst = std::max(worst, static_cast<double>(std::abs(out.laneValues[1500 + i][1] - out.laneValues[3000 + i][1])));
        spread = std::max(spread, static_cast<double>(std::abs(out.laneValues[1500 + i][1] - out.laneValues[1500][1])));
    }
    CHECK(worst < 1e-6);
    CHECK(spread > 0.1);  // it does move
}

TEST_CASE("lane phases and values are reported", "[engine][lanes]") {
    Setup s;
    Engine e;
    e.prepare(s.sampleRate, s.block, s.oversampling);
    render(e, s, held(29), 4800);
    const auto ph = e.lanePhases();
    const auto v = e.laneValues();
    for (int i = 0; i < kLaneCount; ++i) {
        CHECK(ph[static_cast<std::size_t>(i)] >= 0.0f);
        CHECK(ph[static_cast<std::size_t>(i)] < 1.0f);
        CHECK(std::abs(v[static_cast<std::size_t>(i)]) <= 1.0f);
    }
    // Lane 1 is a 1/8 sine at phase 0: after 4800 frames ppq is 0.29, phase frac(0.29 / 0.5).
    const double expected = std::fmod(4800.0 / 48000.0 * 174.0 / 60.0 / 0.5, 1.0);
    CHECK(ph[0] == Catch::Approx(expected).margin(2e-3));
}
