// The tract: five parallel band-passes with alternating sign, one bank per
// channel. Formant frequency, bandwidth and gain come from tf::kVowels and are
// interpolated in the log domain between two vowels. Each vowel has a loudness
// constant `comp` chosen so that broadband noise has the same RMS through every
// vowel; it is computed from the actual digital response in prepare().
#pragma once

#include "svf.h"
#include "tf/vowels.h"

#include <array>
#include <complex>

namespace tf::dsp {

inline constexpr int kVowelCount = static_cast<int>(kVowels.size());
inline constexpr std::array<double, kFormantCount> kFormantSign { 1.0, -1.0, 1.0, -1.0, 1.0 };

// RMS gain of the tract at tract = 1 for white noise band-limited to 20 kHz.
// Below unity because a bass signal concentrates its energy where the
// formants are, and the fold that follows adds plenty of gain.
inline constexpr double kTractNoiseGainDb = -6.0;

// Highest formant frequency, relative to the (oversampled) sample rate.
inline constexpr double kMaxFormantRatio = 0.45;

struct FormantTargets {
    std::array<double, kFormantCount> logHz {};
    std::array<double, kFormantCount> logBandwidth {};
    std::array<double, kFormantCount> gainDb {};
    double compDb = 0.0;
};

inline double clampFormantHz(double hz, double sampleRate) noexcept TF_NONBLOCKING {
    return std::min(hz, kMaxFormantRatio * sampleRate);
}

// Magnitude response of the comp-less formant sum at `hz`, for the digital bank.
inline double formantSumPower(const FormantTargets& t, double hz, double sampleRate) {
    std::complex<double> sum { 0.0, 0.0 };
    const double w = std::tan(kPi * hz / sampleRate);
    for (int i = 0; i < kFormantCount; ++i) {
        const double f = clampFormantHz(std::exp(t.logHz[static_cast<std::size_t>(i)]), sampleRate);
        const double bw = std::exp(t.logBandwidth[static_cast<std::size_t>(i)]);
        const double g = std::tan(kPi * f / sampleRate);
        const double k = bw / f;
        const double omega = w / g;
        const std::complex<double> s { 0.0, omega };
        const std::complex<double> h = k * s / (s * s + k * s + 1.0);
        sum += kFormantSign[static_cast<std::size_t>(i)] * dbToGain(t.gainDb[static_cast<std::size_t>(i)]) * h;
    }
    return std::norm(sum);
}

inline FormantTargets vowelTargets(int vowel) {
    const auto& v = kVowels[static_cast<std::size_t>(std::clamp(vowel, 0, kVowelCount - 1))];
    FormantTargets t;
    for (std::size_t i = 0; i < kFormantCount; ++i) {
        t.logHz[i] = std::log(static_cast<double>(v.hz[i]));
        t.logBandwidth[i] = std::log(static_cast<double>(v.bandwidthHz[i]));
        t.gainDb[i] = v.gainDb[i];
    }
    return t;
}

// Per-vowel loudness constants in dB for a bank running at `sampleRate`.
// Allocation-free but slow; call from prepare().
inline std::array<double, kVowelCount> computeVowelCompDb(double sampleRate) {
    std::array<double, kVowelCount> comp {};
    for (int v = 0; v < kVowelCount; ++v) {
        const auto t = vowelTargets(v);
        double power = 0.0;
        int count = 0;
        for (double hz = 20.0; hz <= 20000.0; hz += 1.0, ++count) power += formantSumPower(t, hz, sampleRate);
        power /= count;
        comp[static_cast<std::size_t>(v)] = kTractNoiseGainDb - 10.0 * std::log10(power);
    }
    return comp;
}

// Formants between vowel a (morph 0) and vowel b (morph 1), log-domain interpolation.
inline FormantTargets mixVowels(int a, int b, double morph, const std::array<double, kVowelCount>& compDb) noexcept
    TF_NONBLOCKING {
    const auto& va = kVowels[static_cast<std::size_t>(std::clamp(a, 0, kVowelCount - 1))];
    const auto& vb = kVowels[static_cast<std::size_t>(std::clamp(b, 0, kVowelCount - 1))];
    const double m = std::clamp(morph, 0.0, 1.0);
    FormantTargets t;
    for (std::size_t i = 0; i < kFormantCount; ++i) {
        t.logHz[i] = (1.0 - m) * std::log(static_cast<double>(va.hz[i])) + m * std::log(static_cast<double>(vb.hz[i]));
        t.logBandwidth[i] = (1.0 - m) * std::log(static_cast<double>(va.bandwidthHz[i])) +
                            m * std::log(static_cast<double>(vb.bandwidthHz[i]));
        t.gainDb[i] = (1.0 - m) * va.gainDb[i] + m * vb.gainDb[i];
    }
    t.compDb = (1.0 - m) * compDb[static_cast<std::size_t>(std::clamp(a, 0, kVowelCount - 1))] +
               m * compDb[static_cast<std::size_t>(std::clamp(b, 0, kVowelCount - 1))];
    return t;
}

// Smoothed formant targets become SVF coefficients at control rate; between
// control points g, k and the gains move linearly, and a1..a3 are recomputed
// from them every sample.
class TractCoefs {
public:
    void setTarget(const FormantTargets& t, double sampleRate, int steps) noexcept TF_NONBLOCKING {
        for (std::size_t i = 0; i < kFormantCount; ++i) {
            const double f = clampFormantHz(std::exp(t.logHz[i]), sampleRate);
            const double bw = std::exp(t.logBandwidth[i]);
            g_[i].to(SvfCoefs::prewarp(f, sampleRate), steps);
            k_[i].to(bw / f, steps);
            gain_[i].to(kFormantSign[i] * dbToGain(t.gainDb[i]), steps);
        }
        comp_.to(dbToGain(t.compDb), steps);
    }
    void snap(const FormantTargets& t, double sampleRate) noexcept TF_NONBLOCKING {
        setTarget(t, sampleRate, 0);
        for (std::size_t i = 0; i < kFormantCount; ++i) {
            g_[i].increment = k_[i].increment = gain_[i].increment = 0.0;
        }
        comp_.increment = 0.0;
        refresh();
    }
    // Moves one sample along the ramps.
    void advance() noexcept TF_NONBLOCKING {
        for (std::size_t i = 0; i < kFormantCount; ++i) {
            g_[i].next();
            k_[i].next();
            gain_[i].next();
        }
        comp_.next();
        refresh();
    }
    const std::array<SvfCoefs, kFormantCount>& coefs() const noexcept TF_NONBLOCKING { return coefs_; }
    const std::array<double, kFormantCount>& gains() const noexcept TF_NONBLOCKING { return gains_; }
    double comp() const noexcept TF_NONBLOCKING { return comp_.value; }

private:
    void refresh() noexcept TF_NONBLOCKING {
        for (std::size_t i = 0; i < kFormantCount; ++i) {
            coefs_[i] = SvfCoefs::fromGK(g_[i].value, k_[i].value);
            gains_[i] = gain_[i].value;
        }
    }

    std::array<Ramp, kFormantCount> g_ {}, k_ {}, gain_ {};
    Ramp comp_ {};
    std::array<SvfCoefs, kFormantCount> coefs_ {};
    std::array<double, kFormantCount> gains_ {};
};

class TractChannel {
public:
    void reset() noexcept TF_NONBLOCKING {
        for (auto& s : state_) s.reset();
    }
    // Returns (1 - amount) x + amount comp sum.
    double process(const TractCoefs& c, double amount, double x) noexcept TF_NONBLOCKING {
        double sum = 0.0;
        const auto& coefs = c.coefs();
        const auto& gains = c.gains();
        for (std::size_t i = 0; i < kFormantCount; ++i) sum += gains[i] * state_[i].bandpass(coefs[i], x);
        return (1.0 - amount) * x + amount * c.comp() * sum;
    }

private:
    std::array<SvfState, kFormantCount> state_ {};
};

}
