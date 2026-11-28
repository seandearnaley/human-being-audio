// SPDX-License-Identifier: AGPL-3.0-or-later
#include "graph_curves.h"

#include "tf/feel.h"
#include "tf/vowels.h"

#include <hba/dsp/adaa.h>

#include <algorithm>
#include <cmath>
#include <complex>

namespace tf::ui {

namespace {
constexpr float kPi = 3.14159265f;
constexpr float kTwoPi = 2.0f * kPi;
constexpr float lerpF(float a, float b, float t) { return a + (b - a) * t; }
} // namespace

float harmonicPosition(float harmonic) { return std::log(harmonic) / std::log(kSculptTop); }

GraphCurve<kShapePoints> subPeriod(float subWarmth) {
    GraphCurve<kShapePoints> values{};
    const float sw = std::clamp(subWarmth, 0.0f, 1.0f);
    for (std::size_t i = 0; i < values.size(); ++i) {
        const float p = kTwoPi * static_cast<float>(i) / static_cast<float>(values.size() - 1);
        const float fund = std::sin(p);
        values[i] = sw <= 1.0e-4f
                        ? fund
                        : (fund + sw * (0.24f * std::sin(2.0f * p) + 0.14f * std::sin(3.0f * p))) / (1.0f + 0.24f * sw);
    }
    return values;
}

// The engine's models (engine/src/voice_core.h), without their DC offsets so
// every model fills the same -1..1 band.
GraphCurve<kSparkPoints> voicePeriod(VoiceModel model, float ratio, float phaseDegrees, float pmIndex, float shape) {
    constexpr float kWarp = 0.85f;
    GraphCurve<kSparkPoints> values{};
    const float phase = phaseDegrees * kPi / 180.0f;
    const float shp = std::clamp(shape, 0.0f, 1.0f);
    const float width = 0.5f - 0.45f * shp;
    const float pdK = std::exp2(4.0f * shp);
    for (std::size_t i = 0; i < values.size(); ++i) {
        const float x = static_cast<float>(i) / static_cast<float>(values.size() - 1);
        const float rad = kTwoPi * x * ratio + phase + pmIndex * std::sin(kTwoPi * x);
        const float cycles = rad / kTwoPi;
        const float t = cycles - std::floor(cycles);
        switch (model) {
        case VoiceModel::sine: {
            const float s1 = std::sin(rad);
            values[i] = (1.0f - shp) * s1 + shp * std::sin(rad + kWarp * shp * s1);
            break;
        }
        case VoiceModel::saw:
            values[i] = 2.0f * (t + kWarp * shp / kTwoPi * std::sin(kTwoPi * t)) - 1.0f;
            break;
        case VoiceModel::pulse:
            values[i] = t < width ? 1.0f : -1.0f;
            break;
        case VoiceModel::pd:
            values[i] = (1.0f - t) * (1.0f - std::cos(kTwoPi * pdK * t)) - 1.0f;
            break;
        }
    }
    return values;
}

// Analogue prototypes of the engine's filters (engine/src/voice_core.h) at s = jw,
// w in multiples of the cutoff. Drive saturates and so has no linear picture.
GraphCurve<kShapePoints> filterResponse(FilterType type, float reach, float resonance) {
    const ParamSpec& range = spec(ParamId::reach);
    const float res = std::clamp(resonance, 0.0f, 1.0f);
    const float cutoff = std::clamp(reach, range.min, range.max);
    const std::complex<float> one(1.0f, 0.0f);
    GraphCurve<kShapePoints> values{};
    for (std::size_t i = 0; i < values.size(); ++i) {
        const float position = static_cast<float>(i) / static_cast<float>(values.size() - 1);
        const float w = range.min * std::pow(range.max / range.min, position) / cutoff;
        const std::complex<float> s(0.0f, w);
        // |numerator| / |denominator|, kept apart so a pole on the axis cannot divide by zero.
        float numerator = 1.0f;
        float denominator = 1.0f;
        switch (type) {
        case FilterType::clean: // 24 dB/oct Butterworth
            denominator = std::sqrt(1.0f + std::pow(w, 8.0f));
            break;
        case FilterType::ladder: { // four poles under negative feedback, passband loss made up part way
            const float k = 4.2f * res;
            numerator = 1.0f + 0.4f * k;
            denominator = std::abs((one + s) * (one + s) * (one + s) * (one + s) + k);
            break;
        }
        case FilterType::ms20: { // Korg 35: two poles, positive feedback through a high-pass
            const float k = 2.1f * res;
            denominator = std::abs(s * s + (2.0f - k) * s + one);
            break;
        }
        case FilterType::lowpass:
        case FilterType::notch: { // state-variable, damping from Butterworth down
            const float damping = std::sqrt(2.0f) * (1.0f - 0.98f * res);
            if (type == FilterType::notch)
                numerator = std::abs(s * s + one);
            denominator = std::abs(s * s + damping * s + one);
            break;
        }
        }
        const float magnitude = numerator / std::max(denominator, 1.0e-6f);
        values[i] = std::clamp(20.0f * std::log10(std::max(magnitude, 1.0e-6f)), kFilterFloorDb, kFilterCeilingDb);
    }
    return values;
}

GraphCurve<kShapePoints> formantResponse(int vowelA, int vowelB, float morph, float shiftSt, float qAmt) {
    constexpr float kLow = 150.0f, kHigh = 5000.0f, kRangeDb = 36.0f;
    const Vowel& a = kVowels[static_cast<std::size_t>(std::clamp(vowelA, 0, 5))];
    const Vowel& b = kVowels[static_cast<std::size_t>(std::clamp(vowelB, 0, 5))];

    const float shiftRatio = std::exp2(std::clamp(shiftSt, -24.0f, 24.0f) / 12.0f);
    const float qNorm = std::clamp(qAmt, 0.0f, 1.0f);
    const float bwScale = qNorm <= 0.5f ? lerpF(2.38f, 1.0f, qNorm * 2.0f) : lerpF(1.0f, 0.42f, (qNorm - 0.5f) * 2.0f);

    std::array<float, kFormantCount> hz{}, bandwidth{}, gain{};
    for (std::size_t k = 0; k < kFormantCount; ++k) {
        hz[k] = std::clamp(std::exp(lerpF(std::log(a.hz[k]), std::log(b.hz[k]), morph)) * shiftRatio, 90.0f, 12000.0f);
        bandwidth[k] =
            std::max(20.0f, std::exp(lerpF(std::log(a.bandwidthHz[k]), std::log(b.bandwidthHz[k]), morph)) * bwScale);
        gain[k] = std::pow(10.0f, lerpF(a.gainDb[k], b.gainDb[k], morph) / 20.0f);
    }

    GraphCurve<kShapePoints> values{};
    float peak = -1000.0f;
    for (std::size_t i = 0; i < values.size(); ++i) {
        const float position = static_cast<float>(i) / static_cast<float>(values.size() - 1);
        const float f = kLow * std::pow(kHigh / kLow, position);
        std::complex<float> sum(0.0f, 0.0f);
        for (std::size_t k = 0; k < kFormantCount; ++k) {
            const float q = hz[k] / bandwidth[k];
            const float detune = f / hz[k] - hz[k] / f;
            const std::complex<float> bp = std::complex<float>(1.0f, 0.0f) / std::complex<float>(1.0f, q * detune);
            const float sign = (k & 1u) == 0u ? 1.0f : -1.0f;
            sum += (sign * gain[k]) * bp;
        }
        values[i] = 20.0f * std::log10(std::max(std::abs(sum), 1.0e-6f));
        peak = std::max(peak, values[i]);
    }
    for (float& v : values)
        v = std::max(0.0f, 1.0f - (peak - v) / kRangeDb);
    return values;
}

GraphCurve<kShapePoints> foldTransfer(FoldType type, float drive, float asym, float extent) {
    const float gain = feel::driveGain(drive);
    const double bias = 0.5 * static_cast<double>(asym);
    GraphCurve<kShapePoints> values{};
    for (std::size_t i = 0; i < values.size(); ++i) {
        const double x =
            static_cast<double>(lerpF(-extent, extent, static_cast<float>(i) / static_cast<float>(values.size() - 1)));
        const double shaped = static_cast<double>(gain) * x + bias;
        double y = shaped;
        switch (type) {
        case FoldType::fold:
            y = hba::dsp::TriangleFoldShaper::eval(shaped) - hba::dsp::TriangleFoldShaper::eval(bias);
            break;
        case FoldType::sine:
            y = hba::dsp::SineFoldShaper::eval(shaped) - hba::dsp::SineFoldShaper::eval(bias);
            break;
        case FoldType::tube:
            y = hba::dsp::TanhShaper::eval(shaped) - hba::dsp::TanhShaper::eval(bias);
            break;
        case FoldType::buchla:
            y = hba::dsp::Buchla259Shaper::eval(shaped) - hba::dsp::Buchla259Shaper::eval(bias);
            break;
        case FoldType::clip:
            y = hba::dsp::HardClipShaper::eval(shaped) - hba::dsp::HardClipShaper::eval(bias);
            break;
        }
        values[i] = static_cast<float>(std::clamp(y, -1.15, 1.15));
    }
    return values;
}

GraphCurve<kSculptPoints> sculptResponse(float sculpt, int count, float spread, float depth, float resonance,
                                         SculptMode mode) {
    GraphCurve<kSculptPoints> values{};
    if (std::abs(depth) < 1.0e-4f)
        return values;
    const float q = feel::sculptQ(resonance);
    std::vector<float> centres;
    for (int i = 0; i < count; ++i) {
        const float centre = sculpt * std::pow(spread, static_cast<float>(i));
        if (centre > kSculptTop)
            break;
        centres.push_back(centre);
    }

    if (mode == SculptMode::bell) {
        const float gainDb = feel::sculptGainDb(depth);
        const float gLin = std::pow(10.0f, gainDb / 20.0f);
        for (std::size_t i = 0; i < values.size(); ++i) {
            const float harmonic = std::pow(kSculptTop, static_cast<float>(i) / static_cast<float>(values.size() - 1));
            float db = 0.0f;
            for (const float centre : centres) {
                const float detune = harmonic / centre - centre / harmonic;
                const std::complex<float> bp = std::complex<float>(1.0f, 0.0f) / std::complex<float>(1.0f, q * detune);
                const std::complex<float> h = std::complex<float>(1.0f, 0.0f) + (gLin - 1.0f) * bp;
                db += 20.0f * std::log10(std::max(std::abs(h), 1.0e-4f));
            }
            values[i] = std::clamp(db, -kSculptRangeDb, kSculptRangeDb);
        }
        return values;
    }

    if (mode == SculptMode::phaser) {
        const float fb = std::clamp(0.75f * resonance, 0.0f, 0.78f);
        const float wet = std::clamp(std::abs(depth), 0.0f, 1.0f);
        const float polarity = depth < 0.0f ? -1.0f : 1.0f;
        for (std::size_t i = 0; i < values.size(); ++i) {
            const float harmonic = std::pow(kSculptTop, static_cast<float>(i) / static_cast<float>(values.size() - 1));
            std::complex<float> ap(1.0f, 0.0f);
            for (const float centre : centres) {
                const float w = harmonic / centre;
                const float r = 1.0f - w * w;
                const float im = w / std::max(0.7f, 0.45f * q);
                ap *= std::complex<float>(r, -im) / std::complex<float>(r, im);
            }
            const std::complex<float> loop = ap / (std::complex<float>(1.0f, 0.0f) - fb * ap);
            const std::complex<float> h = (1.0f - 0.5f * wet) + (0.5f * wet * polarity) * loop;
            values[i] = std::clamp(20.0f * std::log10(std::max(std::abs(h), 1.0e-3f)), -kSculptRangeDb, kSculptRangeDb);
        }
        return values;
    }

    const float fbMag = std::clamp((0.35f + 0.56f * resonance) * std::abs(depth), 0.0f, 0.91f);
    const float fb = depth < 0.0f ? -fbMag : fbMag;
    const float wet = std::clamp(std::abs(depth), 0.0f, 1.0f);
    for (std::size_t i = 0; i < values.size(); ++i) {
        const float harmonic = std::pow(kSculptTop, static_cast<float>(i) / static_cast<float>(values.size() - 1));
        const float phase = kTwoPi * harmonic / std::max(sculpt, 0.5f);
        const float damp = 1.0f / (1.0f + 0.015f * harmonic);
        const std::complex<float> z = std::polar(fb * damp, -phase);
        const std::complex<float> comb = std::complex<float>(1.0f, 0.0f) / (std::complex<float>(1.0f, 0.0f) - z);
        const std::complex<float> h = (1.0f - wet) + wet * (0.45f * comb);
        values[i] = std::clamp(20.0f * std::log10(std::max(std::abs(h), 1.0e-3f)), -kSculptRangeDb, kSculptRangeDb);
    }
    return values;
}

SignalWire& SignalWire::to(float x, float y) {
    points_.push_back({x, y});
    return *this;
}

SignalWire& SignalWire::curveTo(float x, float y) {
    const WirePoint from = points_.back();
    const float handle = (x - from.x) * 0.5f;
    return cubic({from.x + handle, from.y}, {x - handle, y}, {x, y}, 32);
}

SignalWire& SignalWire::turn(float cx, float cy, float x, float y) {
    constexpr float kKappa = 0.5523f;
    const WirePoint from = points_.back();
    return cubic({from.x + (cx - from.x) * kKappa, from.y + (cy - from.y) * kKappa},
                 {x + (cx - x) * kKappa, y + (cy - y) * kKappa}, {x, y}, 10);
}

float SignalWire::length() const {
    float total = 0.0f;
    for (std::size_t i = 1; i < points_.size(); ++i)
        total += distance(points_[i - 1], points_[i]);
    return total;
}

WirePoint SignalWire::at(float s) const {
    for (std::size_t i = 1; i < points_.size(); ++i) {
        const float segment = distance(points_[i - 1], points_[i]);
        if (s <= segment && segment > 0.0f) {
            const float t = s / segment;
            return {lerpF(points_[i - 1].x, points_[i].x, t), lerpF(points_[i - 1].y, points_[i].y, t)};
        }
        s -= segment;
    }
    return points_.back();
}

float SignalWire::distance(WirePoint a, WirePoint b) { return std::hypot(b.x - a.x, b.y - a.y); }

SignalWire& SignalWire::cubic(WirePoint c1, WirePoint c2, WirePoint end, int steps) {
    const WirePoint start = points_.back();
    for (int i = 1; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const float u = 1.0f - t;
        const float a = u * u * u, b = 3.0f * u * u * t, c = 3.0f * u * t * t, d = t * t * t;
        points_.push_back(
            {a * start.x + b * c1.x + c * c2.x + d * end.x, a * start.y + b * c1.y + c * c2.y + d * end.y});
    }
    return *this;
}

} // namespace tf::ui
