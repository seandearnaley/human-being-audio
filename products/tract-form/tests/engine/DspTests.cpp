// Unit tests for the individual DSP pieces under engine/src.
#include "helpers.h"

#include "crossover.h"
#include "decimator.h"
#include "fold.h"
#include "tf/lanes.h"
#include "limiter.h"
#include "squash.h"
#include "svf.h"
#include "tract.h"
#include "voice.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <random>

using namespace tf;
using namespace tf::dsp;
using analysis::Signal;

namespace {

// Magnitude response in dB of an impulse response, per FFT bin.
Signal magnitudeDb(const Signal& impulse, std::size_t n) {
    std::vector<std::complex<double>> buf(n);
    for (std::size_t i = 0; i < std::min(n, impulse.size()); ++i) buf[i] = impulse[i];
    analysis::fft(buf);
    Signal db(n / 2 + 1);
    for (std::size_t i = 0; i < db.size(); ++i) db[i] = 20.0 * std::log10(std::abs(buf[i]) + 1e-300);
    return db;
}

template <class F>
Signal impulseResponse(std::size_t n, F&& process) {
    Signal y(n);
    for (std::size_t i = 0; i < n; ++i) y[i] = process(i == 0 ? 1.0 : 0.0);
    return y;
}

double bandDeviationDb(const Signal& db, double sampleRate, std::size_t n, double loHz, double hiHz) {
    double worst = 0.0;
    for (std::size_t i = 1; i < db.size(); ++i) {
        const double hz = static_cast<double>(i) * sampleRate / static_cast<double>(n);
        if (hz < loHz || hz > hiHz) continue;
        worst = std::max(worst, std::abs(db[i]));
    }
    return worst;
}

}

TEST_CASE("an LR4 low-pass and high-pass pair sums to a flat all-pass", "[dsp][crossover]") {
    const double fs = GENERATE(44100.0, 48000.0, 96000.0);
    const double hz = GENERATE(40.0, 100.0, 200.0, 2500.0);
    CAPTURE(fs, hz);
    const auto c = butterworth(hz, fs);
    constexpr std::size_t n = 1 << 16;

    Lr4Lowpass lp;
    Lr4Highpass hp;
    const auto sum = impulseResponse(n, [&](double x) { return lp.process(c, x) + hp.process(c, x); });
    CHECK(bandDeviationDb(magnitudeDb(sum, n), fs, n, 10.0, 0.49 * fs) < 0.05);

    Lr4Split split;
    const auto splitSum = impulseResponse(n, [&](double x) {
        double lo, hi;
        split.process(c, x, lo, hi);
        return lo + hi;
    });
    CHECK(bandDeviationDb(magnitudeDb(splitSum, n), fs, n, 10.0, 0.49 * fs) < 0.05);

    // And the pair is really a crossover: -6 dB each at the split frequency.
    Lr4Lowpass lp2;
    const auto lowOnly = impulseResponse(n, [&](double x) { return lp2.process(c, x); });
    const auto db = magnitudeDb(lowOnly, n);
    const auto bin = static_cast<std::size_t>(std::lround(hz * n / fs));
    CHECK(db[bin] == Catch::Approx(-6.02).margin(0.2));
}

TEST_CASE("the SVF band-pass peaks at its frequency with unity gain", "[dsp][svf]") {
    const double fs = GENERATE(48000.0, 192000.0);
    const double hz = GENERATE(100.0, 1000.0, 5000.0, 15000.0);
    const double q = GENERATE(2.0, 20.0);
    CAPTURE(fs, hz, q);
    const auto c = SvfCoefs::make(hz, q, fs);
    constexpr std::size_t n = 1 << 18;
    SvfState s;
    const auto y = impulseResponse(n, [&](double x) { return s.bandpass(c, x); });
    const auto db = magnitudeDb(y, n);
    std::size_t peak = 1;
    for (std::size_t i = 1; i < db.size(); ++i)
        if (db[i] > db[peak]) peak = i;
    const double peakHz = static_cast<double>(peak) * fs / n;
    CHECK(std::abs(peakHz - hz) / hz < 0.005);
    // Gain at exactly `hz` (DTFT of the impulse response, not the nearest bin).
    std::complex<double> h { 0, 0 };
    for (std::size_t i = 0; i < n; ++i) h += y[i] * std::polar(1.0, -2.0 * kPi * hz / fs * static_cast<double>(i));
    CHECK(20 * std::log10(std::abs(h)) == Catch::Approx(0.0).margin(0.01));
}

TEST_CASE("every vowel passes broadband noise at the same level at tract = 1", "[dsp][tract]") {
    const int factor = GENERATE(2, 4, 16);
    const double fs = 48000.0 * factor;
    CAPTURE(factor);
    const auto comp = computeVowelCompDb(fs);
    std::mt19937 rng(7);
    std::normal_distribution<double> noise(0.0, 0.1);
    Signal in(static_cast<std::size_t>(fs * 2.0));
    for (auto& v : in) v = noise(rng);

    std::vector<double> levels;
    for (int v = 0; v < kVowelCount; ++v) {
        TractCoefs coefs;
        coefs.snap(mixVowels(v, v, 0.0, comp), fs);
        TractChannel ch;
        Signal out(in.size());
        for (std::size_t i = 0; i < in.size(); ++i) out[i] = ch.process(coefs, 1.0, in[i]);
        const auto skip = static_cast<std::size_t>(0.2 * fs);
        levels.push_back(analysis::rmsDb(test::slice(out, skip, out.size())));
    }
    const double lo = *std::min_element(levels.begin(), levels.end());
    const double hi = *std::max_element(levels.begin(), levels.end());
    INFO("vowel noise levels spread " << hi - lo << " dB");
    CHECK(hi - lo <= 3.0);  // within +-1.5 dB of the centre
    // The reference is white noise band-limited to 20 kHz; this noise is white
    // up to fs/2, so only the share below 20 kHz counts.
    const double inLevel = analysis::rmsDb(in) + 10.0 * std::log10(20000.0 / (fs / 2));
    for (double l : levels) CHECK(l - inLevel == Catch::Approx(kTractNoiseGainDb).margin(1.0));
}

TEST_CASE("formants are clamped below 0.45 fs", "[dsp][tract]") {
    // At 1x and 8 kHz the top formants of every vowel would be above 0.45 fs.
    const double fs = 8000.0;
    const auto comp = computeVowelCompDb(fs);
    TractCoefs coefs;
    coefs.snap(mixVowels(1, 1, 0.0, comp), fs);
    for (const auto& c : coefs.coefs()) CHECK(c.g <= std::tan(kPi * 0.45) + 1e-12);
    TractChannel ch;
    double peak = 0;
    for (int i = 0; i < 8000; ++i) peak = std::max(peak, std::abs(ch.process(coefs, 1.0, i == 0 ? 1.0 : 0.0)));
    CHECK(std::isfinite(peak));
}

TEST_CASE("each shaper's antiderivative matches the shaper", "[dsp][fold]") {
    const auto type = GENERATE(FoldType::fold, FoldType::sine, FoldType::tube);
    for (double u = -40.0; u <= 40.0; u += 0.0137) {
        const double h = 1e-6;
        const double numeric = (shapeAntiderivative(type, u + h) - shapeAntiderivative(type, u - h)) / (2 * h);
        CHECK(numeric == Catch::Approx(shape(type, u)).margin(1e-5));
        CHECK(std::abs(shape(type, u)) <= 1.0 + 1e-12);
    }
    // ADAA of nearly equal inputs falls back to the shaper itself.
    CHECK(adaa(type, 0.3, 0.3) == Catch::Approx(shape(type, 0.3)));
    CHECK(adaa(type, 0.3, 0.3 + 1e-9) == Catch::Approx(shape(type, 0.3)).margin(1e-6));
}

TEST_CASE("the triangle fold is the identity inside +-1 and folds outside", "[dsp][fold]") {
    CHECK(shape(FoldType::fold, 0.5) == Catch::Approx(0.5));
    CHECK(shape(FoldType::fold, 1.0) == Catch::Approx(1.0));
    CHECK(shape(FoldType::fold, 1.5) == Catch::Approx(0.5));
    CHECK(shape(FoldType::fold, 3.0) == Catch::Approx(-1.0));
    CHECK(shape(FoldType::fold, -1.25) == Catch::Approx(-0.75));
}

TEST_CASE("ADAA lowers the aliasing of a hard fold", "[dsp][fold]") {
    // A 1.2 kHz sine folded hard (gain 12) at 96 kHz: compare the inharmonic
    // energy below 20 kHz with and without antiderivative anti-aliasing.
    const double fs = 96000.0, f = 1234.5;
    constexpr std::size_t n = 1 << 16;
    Signal naive(n), smooth(n);
    Folder folder;
    folder.prepare(fs);
    DcBlocker dc;
    dc.prepare(10.0, fs);
    for (std::size_t i = 0; i < n + 4096; ++i) {
        const double x = std::sin(2 * kPi * f * static_cast<double>(i) / fs);
        const double a = folder.process(x, 12.0, 0.1, 1.0, FoldType::fold, FoldType::fold, 1.0);
        const double b = dc.process(shape(FoldType::fold, 12.0 * x + 0.1));
        if (i >= 4096) {
            smooth[i - 4096] = a;
            naive[i - 4096] = b;
        }
    }
    const auto inharmonicDb = [&](const Signal& y) {
        const auto p = analysis::powerSpectrum(y.data(), n, analysis::blackmanHarris(n));
        double harmonic = 0, other = 0;
        for (std::size_t i = 1; i < p.size(); ++i) {
            const double hz = static_cast<double>(i) * fs / n;
            if (hz > 20000.0) break;
            const double k = std::round(hz / f);
            (std::abs(hz - k * f) < 6.0 * fs / n && k >= 1 ? harmonic : other) += p[i];
        }
        return 10 * std::log10(other / harmonic);
    };
    const double a = inharmonicDb(smooth), b = inharmonicDb(naive);
    INFO("ADAA " << a << " dB, naive " << b << " dB");
    CHECK(a < b - 12.0);
}

TEST_CASE("the decimator keeps the audio band and rejects images", "[dsp][decimator]") {
    const int factor = GENERATE(2, 4, 8, 16);
    CAPTURE(factor);
    const double fs = 44100.0, fo = fs * factor;
    Decimator d;
    d.prepare(factor);

    const auto levelDb = [&](double hz) {
        d.reset();
        std::array<float, kMaxOversampling> buf {};
        Signal y;
        long long q = 0;
        for (int n = 0; n < 16384; ++n) {
            for (int j = 0; j < factor; ++j, ++q)
                buf[static_cast<std::size_t>(j)] = static_cast<float>(std::sin(2 * kPi * hz * static_cast<double>(q) / fo));
            y.push_back(d.process(buf.data()));
        }
        return analysis::rmsDb(test::slice(y, 4096, y.size())) + 3.0103;  // relative to a full-scale sine
    };
    // Pass band flat to 20 kHz.
    for (double hz : { 50.0, 1000.0, 10000.0, 19000.0 }) CHECK(std::abs(levelDb(hz)) < 0.01);
    // Anything that would alias into 0..20 kHz is at least 120 dB down (float noise floor ~ -130 dB).
    for (double alias : { 1000.0, 7000.0, 19000.0 })
        for (int k = 1; k < factor; ++k)
            for (double hz : { k * fs - alias, k * fs + alias })
                if (hz < fo / 2) {
                    CAPTURE(hz);
                    CHECK(levelDb(hz) < -120.0);
                }
}

TEST_CASE("lane shapes", "[dsp][lanes]") {
    CHECK(laneShape(LaneShape::sine, 0.25, 0) == Catch::Approx(1.0));
    CHECK(laneShape(LaneShape::triangle, 0.25, 0) == Catch::Approx(1.0));
    CHECK(laneShape(LaneShape::triangle, 0.75, 0) == Catch::Approx(-1.0));
    CHECK(laneShape(LaneShape::triangle, 0.1, 0) == Catch::Approx(0.4));
    CHECK(laneShape(LaneShape::rampUp, 0.0, 0) == Catch::Approx(-1.0));
    CHECK(laneShape(LaneShape::rampDown, 0.0, 0) == Catch::Approx(1.0));
    CHECK(laneShape(LaneShape::square, 0.2, 0) == 1.0);
    CHECK(laneShape(LaneShape::square, 0.7, 0) == -1.0);
    for (int lane = 0; lane < kLaneCount; ++lane) {
        double sum = 0;
        for (int step = 0; step < kLaneStepsPerCycle; ++step) {
            const double v = laneShape(LaneShape::steps, (step + 0.5) / kLaneStepsPerCycle, lane);
            CHECK(v >= -1.0);
            CHECK(v <= 1.0);
            CHECK(v == laneStepValue(lane, step));
            sum += std::abs(v);
        }
        CHECK(sum > 0.5);  // not all zero
    }
    CHECK(laneStepValue(0, 0) != laneStepValue(1, 0));
}

TEST_CASE("lane phase follows the host position", "[dsp][lanes]") {
    // 1/4 lane (index 5) with a 90 degree offset.
    CHECK(lanePhase(0.0, kLaneRateQuarters[5], 90.0) == Catch::Approx(0.25));
    CHECK(lanePhase(2.5, kLaneRateQuarters[5], 0.0) == Catch::Approx(0.5));
    CHECK(lanePhase(-0.25, kLaneRateQuarters[5], 0.0) == Catch::Approx(0.75));
    // 8 bars = 32 quarters.
    CHECK(lanePhase(16.0, kLaneRateQuarters[0], 0.0) == Catch::Approx(0.5));
}

TEST_CASE("the note stack gives last-note priority", "[dsp][voice]") {
    NoteStack s;
    s.push(40);
    s.push(45);
    s.push(43);
    CHECK(s.top() == 43);
    s.remove(43);
    CHECK(s.top() == 45);
    s.remove(40);
    CHECK(s.top() == 45);
    s.push(40);
    s.push(45);  // re-pressing moves it to the top, no duplicate
    CHECK(s.size() == 2);
    CHECK(s.top() == 45);
    s.remove(45);
    CHECK(s.top() == 40);
    s.remove(40);
    CHECK(s.empty());
}

TEST_CASE("glide is linear in pitch over its time", "[dsp][voice]") {
    Glide g;
    g.jump(40);
    g.glideTo(52, 1000.0);
    CHECK(g.advance(500.0) == Catch::Approx(46.0));
    CHECK(g.advance(499.0) == Catch::Approx(51.988));
    CHECK(g.advance(10.0) == Catch::Approx(52.0));
    g.glideTo(40, 0.0);
    CHECK(g.current() == 40.0);
}

TEST_CASE("the gate ramps 2 ms up, 30 ms down and fades in 1 ms", "[dsp][voice]") {
    Gate g;
    g.prepare(48000.0);
    g.open();
    int n = 0;
    while (g.value() < 1.0) {
        g.step();
        ++n;
    }
    CHECK(std::abs(n - 96) <= 1);
    g.close();
    n = 0;
    while (g.value() > 0.0) {
        g.step();
        ++n;
    }
    CHECK(std::abs(n - 1440) <= 1);
    g.open();
    for (int i = 0; i < 200; ++i) g.step();
    g.fade();
    n = 0;
    bool done = false;
    while (!done) {
        done = g.step();
        ++n;
    }
    CHECK(std::abs(n - 48) <= 1);
}

TEST_CASE("squash at 0 is flat", "[dsp][squash]") {
    const double fs = GENERATE(44100.0, 48000.0, 96000.0);
    Squash s;
    s.prepare(fs);
    constexpr std::size_t n = 1 << 16;
    const auto y = impulseResponse(n, [&](double x) {
        double l = x, r = x;
        s.process(l, r, 0.0);
        return l;
    });
    CHECK(bandDeviationDb(magnitudeDb(y, n), fs, n, 10.0, 0.49 * fs) < 0.05);
}

TEST_CASE("squash compresses loud bands and lifts quiet ones", "[dsp][squash]") {
    CHECK(SquashCurve::gainDb(-5.0, 1.0) < -20.0);
    CHECK(SquashCurve::gainDb(-60.0, 1.0) > 6.0);
    CHECK(SquashCurve::gainDb(-200.0, 1.0) == 0.0);
    CHECK(SquashCurve::gainDb(-5.0, 0.0) == 0.0);
}

TEST_CASE("squash make-up never lowers the level, loud or quiet", "[dsp][squash]") {
    // A loud and a quiet two-tone through Squash at increasing amounts: the
    // output RMS never drops as the amount rises.
    const double fs = 48000.0;
    for (double level : { 0.9, 0.003 }) {
        CAPTURE(level);
        double previous = -1e9;
        for (double amount : { 0.0, 0.25, 0.5, 0.75, 1.0 }) {
            Squash s;
            s.prepare(fs);
            Signal out;
            for (int i = 0; i < 96000; ++i) {
                const double t = i / fs;
                double l = level * (0.7 * std::sin(2 * kPi * 330 * t) + 0.3 * std::sin(2 * kPi * 3300 * t));
                double r = l;
                s.process(l, r, amount);
                if (i >= 48000) out.push_back(l);
            }
            const double db = analysis::rmsDb(out);
            INFO("amount " << amount << ": " << db << " dB");
            CHECK(db >= previous - 0.05);
            CHECK(std::isfinite(db));
            previous = db;
        }
    }
}

TEST_CASE("the limiter holds a hot signal under the ceiling, true peak included", "[dsp][limiter]") {
    const double ceilingDb = GENERATE(-0.3, -6.0);
    const double fs = 48000.0;
    Limiter lim;
    lim.prepare(fs);
    CHECK(lim.latency() == 72 - 1 + TruePeakDetector::kDelay);
    std::mt19937 rng(3);
    std::normal_distribution<double> noise(0.0, 1.0);
    Signal outL;
    const double ceiling = std::pow(10.0, ceilingDb / 20.0);
    for (int i = 0; i < 96000; ++i) {
        // Bursts of loud tones near Nyquist (strong inter-sample peaks) and noise.
        const double t = i / fs;
        const double burst = (i / 4800) % 2 == 0 ? 4.0 : 0.3;
        double l = burst * (std::sin(2 * kPi * 11025.0 * 1.003 * t + 0.4) + 0.2 * noise(rng));
        double r = 0.5 * l;
        std::array<double, 2> taps { 0, 0 };
        lim.process(l, r, taps, ceiling, 1.0);
        outL.push_back(l);
    }
    const double tp = analysis::truePeak4x(outL);
    INFO("true peak " << analysis::toDb(tp) << " dBFS, ceiling " << ceilingDb);
    CHECK(analysis::toDb(tp) <= ceilingDb + 0.1);
}

TEST_CASE("the limiter's delay is the same whether it limits or not", "[dsp][limiter]") {
    Limiter on, off;
    on.prepare(48000.0);
    off.prepare(48000.0);
    for (int i = 0; i < 1000; ++i) {
        double l1 = i == 500 ? 0.5 : 0.0, r1 = 0, l2 = l1, r2 = 0;
        std::array<double, 2> t1 { 0, 0 }, t2 { 0, 0 };
        on.process(l1, r1, t1, 1.0, 1.0);
        off.process(l2, r2, t2, 1.0, 0.0);
        CHECK(l1 == l2);
        if (i == 500 + on.latency()) CHECK(l1 == 0.5);
    }
}

TEST_CASE("the soft clip is transparent below its knee and bounded above", "[dsp][limiter]") {
    CHECK(softClip(0.5) == 0.5);
    CHECK(softClip(-0.8) == -0.8);
    CHECK(softClip(0.9) < 0.9);
    CHECK(softClip(100.0) <= 1.0);
    CHECK(softClip(-100.0) >= -1.0);
    // Continuous slope at the knee.
    const double h = 1e-7;
    CHECK((softClip(kSoftClipKnee + h) - softClip(kSoftClipKnee)) / h == Catch::Approx(1.0).margin(1e-4));
}

TEST_CASE("the feedback delay interpolates fractional delays", "[dsp][feedback]") {
    FeedbackDelay d;
    d.prepare(1000);
    // A ramp makes Lagrange interpolation exact.
    for (int i = 0; i < 200; ++i) d.write(static_cast<float>(i));
    // Most recent write (199) is delay 1.
    CHECK(d.read(1.0 + 1.0) == Catch::Approx(198.0));
    CHECK(d.read(10.25) == Catch::Approx(199.0 - 9.25));
    CHECK(d.read(57.5) == Catch::Approx(199.0 - 56.5));
    d.clear();
    CHECK(d.read(10.0) == 0.0);
}
