#include "analysis.h"

#include <hba/core/wav.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <numeric>
#include <sstream>

namespace tf::analysis {

namespace {
constexpr double kPi = 3.14159265358979323846;

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;
    double process(double x) {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

Biquad butterworth(double hz, double sampleRate, bool highpass) {
    const double w0 = 2.0 * kPi * hz / sampleRate;
    const double c = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * std::sqrt(0.5));
    const double a0 = 1.0 + alpha;
    Biquad q;
    if (highpass) {
        q.b0 = (1.0 + c) / 2.0 / a0;
        q.b1 = -(1.0 + c) / a0;
        q.b2 = q.b0;
    } else {
        q.b0 = (1.0 - c) / 2.0 / a0;
        q.b1 = (1.0 - c) / a0;
        q.b2 = q.b0;
    }
    q.a1 = -2.0 * c / a0;
    q.a2 = (1.0 - alpha) / a0;
    return q;
}

Signal lr4(const Signal& x, double hz, double sampleRate, bool highpass) {
    Biquad a = butterworth(hz, sampleRate, highpass), b = a;
    Signal y(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) y[i] = b.process(a.process(x[i]));
    return y;
}

double besselI0(double x) {
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 60; ++k) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
    }
    return sum;
}
}

void fft(std::vector<std::complex<double>>& a) {
    const std::size_t n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
        const double angle = -2.0 * kPi / static_cast<double>(len);
        for (std::size_t i = 0; i < n; i += len) {
            for (std::size_t k = 0; k < len / 2; ++k) {
                const std::complex<double> w = std::polar(1.0, angle * static_cast<double>(k));
                const auto u = a[i + k];
                const auto v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }
        }
    }
}

Signal blackmanHarris(std::size_t n) {
    Signal w(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double t = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(n);
        w[i] = 0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t);
    }
    return w;
}

Signal hann(std::size_t n) {
    Signal w(n);
    for (std::size_t i = 0; i < n; ++i) w[i] = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(n));
    return w;
}

Signal powerSpectrum(const double* x, std::size_t n, const Signal& window) {
    std::vector<std::complex<double>> buf(n);
    for (std::size_t i = 0; i < n; ++i) buf[i] = { x[i] * window[i], 0.0 };
    fft(buf);
    Signal p(n / 2 + 1);
    for (std::size_t i = 0; i <= n / 2; ++i) p[i] = std::norm(buf[i]);
    return p;
}

Signal welch(const Signal& x, std::size_t frame) {
    const Signal w = hann(frame);
    Signal sum(frame / 2 + 1, 0.0);
    int count = 0;
    for (std::size_t start = 0; start + frame <= x.size(); start += frame / 2, ++count) {
        const Signal p = powerSpectrum(x.data() + start, frame, w);
        for (std::size_t i = 0; i < p.size(); ++i) sum[i] += p[i];
    }
    if (count > 0)
        for (auto& v : sum) v /= count;
    return sum;
}

double rms(const double* x, std::size_t n) {
    if (n == 0) return 0.0;
    double s = 0.0;
    for (std::size_t i = 0; i < n; ++i) s += x[i] * x[i];
    return std::sqrt(s / static_cast<double>(n));
}

double toDb(double linear) { return linear > 1e-15 ? 20.0 * std::log10(linear) : -300.0; }
double rmsDb(const Signal& x) { return toDb(rms(x.data(), x.size())); }

Signal lr4Lowpass(const Signal& x, double hz, double sampleRate) { return lr4(x, hz, sampleRate, false); }
Signal lr4Highpass(const Signal& x, double hz, double sampleRate) { return lr4(x, hz, sampleRate, true); }

double truePeak4x(const Signal& x) {
    constexpr int phases = 4;
    constexpr int half = 48;  // base-rate samples either side
    constexpr int length = 2 * half * phases + 1;
    constexpr double centre = (length - 1) / 2.0;
    static const std::vector<double> taps = [] {
        std::vector<double> h(length);
        for (int t = 0; t < length; ++t) {
            const double u = (t - centre) / phases;
            const double sinc = u == 0.0 ? 1.0 : std::sin(kPi * u) / (kPi * u);
            const double r = (t - centre) / centre;
            h[static_cast<std::size_t>(t)] = sinc * besselI0(10.0 * std::sqrt(std::max(0.0, 1.0 - r * r))) / besselI0(10.0);
        }
        return h;
    }();
    double peak = 0.0;
    const auto n = static_cast<long long>(x.size());
    for (long long i = 0; i < n; ++i) {
        peak = std::max(peak, std::abs(x[static_cast<std::size_t>(i)]));
        for (int r = 1; r < phases; ++r) {
            // Output at time i + r/4 = sum over m of h[r + 4m] x[i + half - m].
            double v = 0.0;
            for (int m = 0; m < 2 * half; ++m) {
                const long long k = i + half - m;
                if (k < 0 || k >= n) continue;
                v += taps[static_cast<std::size_t>(r + phases * m)] * x[static_cast<std::size_t>(k)];
            }
            peak = std::max(peak, std::abs(v));
        }
    }
    return peak;
}

double correlation(const Signal& l, const Signal& r) {
    double lr = 0, ll = 0, rr = 0;
    for (std::size_t i = 0; i < std::min(l.size(), r.size()); ++i) {
        lr += l[i] * r[i];
        ll += l[i] * l[i];
        rr += r[i] * r[i];
    }
    return ll > 0 && rr > 0 ? lr / std::sqrt(ll * rr) : 0.0;
}

Stereo deinterleave(const std::vector<float>& interleaved) {
    Stereo s;
    const std::size_t frames = interleaved.size() / 2;
    s.left.resize(frames);
    s.right.resize(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        s.left[i] = interleaved[2 * i];
        s.right[i] = interleaved[2 * i + 1];
    }
    return s;
}

Signal windowedRmsDb(const Signal& x, std::size_t begin, std::size_t end, double sampleRate, double seconds) {
    // Hann-weighted mean square: a rectangular 50 ms window over a 40 Hz sine
    // holds a non-integer number of periods and alone wobbles by ~0.6 dB.
    const auto window = static_cast<std::size_t>(std::llround(seconds * sampleRate));
    const Signal w = hann(window);
    const double norm = std::accumulate(w.begin(), w.end(), 0.0);
    Signal out;
    for (std::size_t start = begin; start + window <= end; start += window / 2) {
        double s = 0.0;
        for (std::size_t i = 0; i < window; ++i) s += w[i] * x[start + i] * x[start + i];
        out.push_back(toDb(std::sqrt(s / norm)));
    }
    return out;
}

double sideBelowXoverDb(const Stereo& audio, std::size_t begin, std::size_t end, double sampleRate, double xover) {
    Signal mid(audio.left.begin() + static_cast<long>(begin), audio.left.begin() + static_cast<long>(end));
    Signal side = mid;
    for (std::size_t i = 0; i < mid.size(); ++i) {
        const double l = audio.left[begin + i], r = audio.right[begin + i];
        mid[i] = 0.5 * (l + r);
        side[i] = 0.5 * (l - r);
    }
    std::size_t frame = 1;
    while (frame * 2 <= std::min<std::size_t>(mid.size(), 32768)) frame *= 2;
    const Signal pm = welch(mid, frame), ps = welch(side, frame);
    double em = 0, es = 0;
    for (std::size_t i = 1; i < pm.size(); ++i) {
        const double hz = static_cast<double>(i) * sampleRate / static_cast<double>(frame);
        if (hz >= xover) break;
        em += pm[i];
        es += ps[i];
    }
    if (em <= 0) return -300.0;
    return std::max(-300.0, 10.0 * std::log10(std::max(es, 1e-300) / em));  // -300 = no side at all
}

Signal centroids(const Signal& x, std::size_t begin, std::size_t end, double sampleRate) {
    constexpr std::size_t frame = 4096, hop = 2048;
    const Signal w = hann(frame);
    Signal out;
    for (std::size_t start = begin; start + frame <= end; start += hop) {
        const Signal p = powerSpectrum(x.data() + start, frame, w);
        double num = 0, den = 0;
        for (std::size_t i = 1; i < p.size(); ++i) {
            const double hz = static_cast<double>(i) * sampleRate / static_cast<double>(frame);
            if (hz < 20.0) continue;
            if (hz > 20000.0) break;
            const double mag = std::sqrt(p[i]);
            num += hz * mag;
            den += mag;
        }
        if (den > 0) out.push_back(num / den);
    }
    return out;
}

double percentile(Signal values, double p) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const double pos = std::clamp(p, 0.0, 1.0) * static_cast<double>(values.size() - 1);
    const auto lo = static_cast<std::size_t>(std::floor(pos));
    const auto hi = std::min(values.size() - 1, lo + 1);
    return values[lo] + (values[hi] - values[lo]) * (pos - static_cast<double>(lo));
}

Signal welchBlackmanHarris(const Signal& x, std::size_t frame) {
    const Signal w = blackmanHarris(frame);
    Signal sum(frame / 2 + 1, 0.0);
    int count = 0;
    for (std::size_t start = 0; start + frame <= x.size(); start += frame / 2, ++count) {
        const Signal p = powerSpectrum(x.data() + start, frame, w);
        for (std::size_t i = 0; i < p.size(); ++i) sum[i] += p[i];
    }
    if (count > 0)
        for (auto& v : sum) v /= count;
    return sum;
}

std::size_t powerOfTwoAtMost(std::size_t n, std::size_t cap) {
    std::size_t p = 1;
    while (p * 2 <= std::min(n, cap)) p *= 2;
    return p;
}

namespace {
Signal steadySpectrum(const Signal& x, std::size_t begin, std::size_t end, std::size_t& frame) {
    end = std::min(end, x.size());
    begin = std::min(begin, end);
    const Signal part(x.begin() + static_cast<long>(begin), x.begin() + static_cast<long>(end));
    frame = powerOfTwoAtMost(part.size(), std::size_t { 1 } << 16);
    return welchBlackmanHarris(part, frame);
}
}

std::optional<double> tiltDbPerOct(const Signal& x, std::size_t begin, std::size_t end, double sampleRate,
                                   double upperHz) {
    constexpr double kLowHz = 500.0;
    const double upper = std::min({ upperHz, 16000.0, 0.5 * sampleRate });
    if (!(upper >= 4.0 * kLowHz)) return std::nullopt;
    std::size_t frame = 1;
    const Signal p = steadySpectrum(x, begin, end, frame);
    if (frame < 256) return std::nullopt;
    const double binHz = sampleRate / static_cast<double>(frame);
    // Least squares over (octave index, band energy in dB).
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int bands = 0;
    for (int octave = 0; octave < 5; ++octave) {
        const double lo = kLowHz * std::exp2(octave), hi = 2.0 * lo;
        if (hi > upper * (1.0 + 1e-9)) break;
        double sum = 0;
        int count = 0;
        for (std::size_t i = 1; i < p.size(); ++i) {
            const double hz = static_cast<double>(i) * binHz;
            if (hz < lo) continue;
            if (hz >= hi) break;
            sum += p[i];
            ++count;
        }
        if (count == 0) continue;
        const double y = 10.0 * std::log10(std::max(sum, 1e-300));
        const double xo = octave;
        sx += xo;
        sy += y;
        sxx += xo * xo;
        sxy += xo * y;
        ++bands;
    }
    if (bands < 2) return std::nullopt;
    const double n = bands;
    return (n * sxy - sx * sy) / (n * sxx - sx * sx);
}

double offHarmonicDb(const Signal& x, std::size_t begin, std::size_t end, double sampleRate, double f0,
                     double toleranceHz) {
    if (f0 <= 0.0) return 0.0;
    std::size_t frame = 1;
    const Signal p = steadySpectrum(x, begin, end, frame);
    const double binHz = sampleRate / static_cast<double>(frame);
    double total = 0, off = 0;
    for (std::size_t i = 1; i < p.size(); ++i) {
        const double hz = static_cast<double>(i) * binHz;
        if (hz < 300.0) continue;
        if (hz > 6000.0) break;
        total += p[i];
        const double k = std::max(1.0, std::round(hz / f0));
        if (std::abs(hz - k * f0) > toleranceHz) off += p[i];
    }
    if (total <= 0) return -300.0;
    return std::max(-300.0, 10.0 * std::log10(std::max(off, 1e-300) / total));
}

double harmonicToleranceHz(double voiceOffsetAHz, double voiceOffsetBHz) {
    return std::max(kMinHarmonicToleranceHz, 1.5 * std::max(std::abs(voiceOffsetAHz), std::abs(voiceOffsetBHz)));
}

double bandPowerDb(const Signal& x, std::size_t begin, std::size_t end, double sampleRate, double lo, double hi) {
    std::size_t frame = 1;
    const Signal p = steadySpectrum(x, begin, end, frame);
    const double binHz = sampleRate / static_cast<double>(frame);
    double sum = 0;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const double hz = static_cast<double>(i) * binHz;
        if (hz >= lo && hz < hi) sum += p[i];
    }
    return 10.0 * std::log10(std::max(sum, 1e-300));
}

Metrics computeMetrics(const Stereo& audio, const MetricsRequest& q) {
    Metrics m;
    const std::size_t frames = audio.left.size();
    const std::size_t begin = std::min(q.steadyBegin, frames);
    const std::size_t end = std::clamp(q.steadyEnd, begin, frames);
    const std::size_t n = end - begin;

    double peak = 0;
    for (std::size_t i = 0; i < frames; ++i) peak = std::max({ peak, std::abs(audio.left[i]), std::abs(audio.right[i]) });
    m.peakDb = toDb(peak);
    m.truePeakDb = toDb(std::max(truePeak4x(audio.left), truePeak4x(audio.right)));

    const double power = 0.5 * (std::pow(rms(audio.left.data() + begin, n), 2) + std::pow(rms(audio.right.data() + begin, n), 2));
    m.rmsDb = toDb(std::sqrt(power));
    m.crestDb = m.peakDb - m.rmsDb;

    Signal mono(frames);
    for (std::size_t i = 0; i < frames; ++i) mono[i] = 0.5 * (audio.left[i] + audio.right[i]);
    const Signal sub = lr4Lowpass(mono, q.subFreq, q.sampleRate);
    m.subRmsDb = toDb(rms(sub.data() + begin, n));
    const Signal windows = windowedRmsDb(sub, begin, end, q.sampleRate);
    if (!windows.empty()) m.subRmsRangeDb = *std::max_element(windows.begin(), windows.end()) - *std::min_element(windows.begin(), windows.end());

    m.sideBelowXoverDb = sideBelowXoverDb(audio, begin, end, q.sampleRate, q.xover);
    m.correlation = correlation(Signal(audio.left.begin() + static_cast<long>(begin), audio.left.begin() + static_cast<long>(end)),
                                Signal(audio.right.begin() + static_cast<long>(begin), audio.right.begin() + static_cast<long>(end)));

    const Signal c = centroids(mono, begin, end, q.sampleRate);
    if (!c.empty()) {
        m.centroidHz = std::accumulate(c.begin(), c.end(), 0.0) / static_cast<double>(c.size());
        m.centroidRangeHz = percentile(c, 0.95) - percentile(c, 0.05);
    }

    const Signal hl = lr4Highpass(audio.left, q.subFreq, q.sampleRate), hr = lr4Highpass(audio.right, q.subFreq, q.sampleRate);
    m.growlRmsDb = toDb(std::sqrt(0.5 * (std::pow(rms(hl.data() + begin, n), 2) + std::pow(rms(hr.data() + begin, n), 2))));

    double steadyPeak = 0;
    for (std::size_t i = begin; i < end; ++i) steadyPeak = std::max({ steadyPeak, std::abs(audio.left[i]), std::abs(audio.right[i]) });
    m.sumCrestDb = toDb(steadyPeak) - m.rmsDb;
    m.tiltDbPerOct = tiltDbPerOct(mono, begin, end, q.sampleRate, q.tiltUpperHz);
    m.harmonicToleranceHz = q.harmonicToleranceHz;
    m.offHarmonicDb = offHarmonicDb(mono, begin, end, q.sampleRate, q.f0, q.harmonicToleranceHz);
    return m;
}

std::string toJson(const Metrics& m) {
    char tilt[32] = "null";
    if (m.tiltDbPerOct) std::snprintf(tilt, sizeof tilt, "%.2f", *m.tiltDbPerOct);
    char buf[1024];
    std::snprintf(buf, sizeof buf,
                  "{\"peakDb\": %.2f, \"truePeakDb\": %.2f, \"rmsDb\": %.2f, \"crestDb\": %.2f, \"subRmsDb\": %.2f, "
                  "\"subRmsRangeDb\": %.3f, \"sideBelowXoverDb\": %.2f, \"correlation\": %.4f, \"centroidHz\": %.1f, "
                  "\"centroidRangeHz\": %.1f, \"growlRmsDb\": %.2f, \"sumCrestDb\": %.2f, \"tiltDbPerOct\": %s, "
                  "\"offHarmonicDb\": %.1f, \"harmonicToleranceHz\": %.2f}",
                  m.peakDb, m.truePeakDb, m.rmsDb, m.crestDb, m.subRmsDb, m.subRmsRangeDb, m.sideBelowXoverDb, m.correlation,
                  m.centroidHz, m.centroidRangeHz, m.growlRmsDb, m.sumCrestDb, tilt, m.offHarmonicDb,
                  m.harmonicToleranceHz);
    return buf;
}

bool writeWavFloat(const std::string& path, const std::vector<float>& interleaved, int channels, int sampleRate) {
    return hba::core::writeWavFloat(path, interleaved, channels, sampleRate);
}

}
