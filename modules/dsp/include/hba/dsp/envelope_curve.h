// SPDX-License-Identifier: GPL-3.0-or-later
// Non-periodic piecewise envelope spline with exact Gauss-Legendre analytical
// frequency-integral evaluation for phase-continuous kick & percussion pitch sweeps.
#pragma once

#include <hba/dsp/control.h>
#include <hba/dsp/nonblocking.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace hba::dsp {

inline constexpr int kMaxEnvNodes = 16;

enum class EnvSegment : std::uint8_t {
    line = 0,   // power-tension line between nodes
    exp = 1,    // exponential ease (musical pitch drop / decay)
    smooth = 2, // half-cosine S-curve (zero slope at both ends)
    hold = 3,   // step hold until next knot
};

struct EnvNode {
    float t = 0.0f;       // normalized time, 0..1
    float y = 0.0f;       // normalized value, 0..1
    float tension = 0.0f; // -1..+1 curvature (- bends down/fast drop, + bends up/slow drop)
    EnvSegment seg = EnvSegment::line;

    friend bool operator==(const EnvNode&, const EnvNode&) = default;
};

struct EnvelopeCurve {
    std::array<EnvNode, kMaxEnvNodes> nodes{};
    int count = 0;

    friend bool operator==(const EnvelopeCurve& a, const EnvelopeCurve& b) noexcept HBA_NONBLOCKING {
        if (a.count != b.count)
            return false;
        const int n = std::clamp(a.count, 0, kMaxEnvNodes);
        for (int i = 0; i < n; ++i) {
            if (!(a.nodes[static_cast<std::size_t>(i)] == b.nodes[static_cast<std::size_t>(i)]))
                return false;
        }
        return true;
    }
};

namespace detail {

inline double envSegmentShape(EnvSegment seg, double tension, double u) noexcept HBA_NONBLOCKING {
    const double uc = std::clamp(u, 0.0, 1.0);
    switch (seg) {
    case EnvSegment::hold:
        return 0.0;
    case EnvSegment::smooth:
        return 0.5 - 0.5 * std::cos(kPi * uc);
    case EnvSegment::exp: {
        // Exponential shaping parameterized by tension in [-1, 1] (default -0.55 = brisk pitch drop)
        const double k = std::clamp(tension, -1.0, 1.0) * 6.0;
        if (std::abs(k) < 1.0e-4)
            return uc;
        return std::expm1(k * uc) / std::expm1(k);
    }
    case EnvSegment::line:
    default: {
        if (std::abs(tension) < 1.0e-4)
            return uc;
        // Negative tension -> drops/rises fast early; positive tension -> eases early
        const double p = std::pow(2.0, std::clamp(tension, -1.0, 1.0) * 3.0);
        return std::pow(uc, p);
    }
    }
}

inline double evalSegmentY(const EnvNode& a, const EnvNode& b, double t) noexcept HBA_NONBLOCKING {
    const double dt = static_cast<double>(b.t) - static_cast<double>(a.t);
    if (dt <= 1.0e-9)
        return static_cast<double>(b.y);
    const double u = std::clamp((t - static_cast<double>(a.t)) / dt, 0.0, 1.0);
    const double s = envSegmentShape(a.seg, static_cast<double>(a.tension), u);
    return static_cast<double>(a.y) + (static_cast<double>(b.y) - static_cast<double>(a.y)) * s;
}

} // namespace detail

inline double evaluateEnvelope(const EnvelopeCurve& c, double t) noexcept HBA_NONBLOCKING {
    if (c.count <= 0 || !std::isfinite(t))
        return 0.0;
    const double tc = std::clamp(t, 0.0, 1.0);
    if (c.count == 1 || tc <= static_cast<double>(c.nodes[0].t))
        return std::clamp(static_cast<double>(c.nodes[0].y), 0.0, 1.0);
    const int last = std::min(c.count, kMaxEnvNodes) - 1;
    if (tc >= static_cast<double>(c.nodes[static_cast<std::size_t>(last)].t))
        return std::clamp(static_cast<double>(c.nodes[static_cast<std::size_t>(last)].y), 0.0, 1.0);

    for (int i = 0; i < last; ++i) {
        const auto& a = c.nodes[static_cast<std::size_t>(i)];
        const auto& b = c.nodes[static_cast<std::size_t>(i + 1)];
        if (tc <= static_cast<double>(b.t))
            return std::clamp(detail::evalSegmentY(a, b, tc), 0.0, 1.0);
    }
    return std::clamp(static_cast<double>(c.nodes[static_cast<std::size_t>(last)].y), 0.0, 1.0);
}

inline double envelopeNormToLogHz(double y, double minHz, double maxHz) noexcept HBA_NONBLOCKING {
    const double lo = std::max(1.0, minHz);
    const double hi = std::max(lo + 1.0, maxHz);
    return lo * std::pow(hi / lo, std::clamp(y, 0.0, 1.0));
}

inline double logHzToEnvelopeNorm(double hz, double minHz, double maxHz) noexcept HBA_NONBLOCKING {
    const double lo = std::max(1.0, minHz);
    const double hi = std::max(lo + 1.0, maxHz);
    const double clamped = std::clamp(hz, lo, hi);
    return std::clamp(std::log(clamped / lo) / std::log(hi / lo), 0.0, 1.0);
}

// Evaluates exact integral_{t0}^{t1} f(tau) dtau (in normalized time units; multiply by totalSeconds
// to obtain phase cycles!) via 4-point Gauss-Legendre quadrature split cleanly at any interior knot.
inline double integrateEnvelopeLogHz(const EnvelopeCurve& c, double t0, double t1, double minHz,
                                     double maxHz) noexcept HBA_NONBLOCKING {
    if (!(t1 > t0))
        return 0.0;
    const double a = std::clamp(t0, 0.0, 1.0);
    const double b = std::clamp(t1, 0.0, 1.0);
    if (!(b > a))
        return 0.0;

    // 4-point Gauss-Legendre nodes & weights on [-1, 1]
    constexpr std::array<double, 4> kX{-0.8611363115940526, -0.3399810435848563, 0.3399810435848563,
                                       0.8611363115940526};
    constexpr std::array<double, 4> kW{0.3478548451374538, 0.6521451548625461, 0.6521451548625461, 0.3478548451374538};

    const auto quadSub = [&](double lo, double hi) noexcept HBA_NONBLOCKING -> double {
        const double span = hi - lo;
        const int panels = std::clamp(static_cast<int>(std::ceil(span / 0.02)), 1, 64);
        const double step = span / static_cast<double>(panels);
        double total = 0.0;
        for (int p = 0; p < panels; ++p) {
            const double pLo = lo + static_cast<double>(p) * step;
            const double mid = pLo + 0.5 * step;
            const double half = 0.5 * step;
            double sum = 0.0;
            for (std::size_t q = 0; q < 4; ++q) {
                const double y = evaluateEnvelope(c, mid + half * kX[q]);
                sum += kW[q] * envelopeNormToLogHz(y, minHz, maxHz);
            }
            total += half * sum;
        }
        return total;
    };

    double acc = 0.0;
    double cur = a;
    const int n = std::min(c.count, kMaxEnvNodes);
    for (int i = 0; i < n; ++i) {
        const double kt = static_cast<double>(c.nodes[static_cast<std::size_t>(i)].t);
        if (kt > cur + 1.0e-9 && kt < b - 1.0e-9) {
            acc += quadSub(cur, kt);
            cur = kt;
        }
    }
    acc += quadSub(cur, b);
    return acc;
}

inline void sanitizeEnvelope(EnvelopeCurve& c) noexcept HBA_NONBLOCKING {
    c.count = std::clamp(c.count, 0, kMaxEnvNodes);
    if (c.count < 2) {
        c.count = 2;
        c.nodes[0] = {0.0f, 1.0f, 0.0f, EnvSegment::line};
        c.nodes[1] = {1.0f, 0.0f, 0.0f, EnvSegment::line};
        return;
    }
    for (int i = 0; i < c.count; ++i) {
        auto& n = c.nodes[static_cast<std::size_t>(i)];
        n.t = std::isfinite(n.t) ? std::clamp(n.t, 0.0f, 1.0f) : 0.0f;
        n.y = std::isfinite(n.y) ? std::clamp(n.y, 0.0f, 1.0f) : 0.0f;
        n.tension = std::isfinite(n.tension) ? std::clamp(n.tension, -1.0f, 1.0f) : 0.0f;
    }
    // Insertion sort (allocation-free, real-time safe)
    for (int i = 1; i < c.count; ++i) {
        const EnvNode key = c.nodes[static_cast<std::size_t>(i)];
        int j = i - 1;
        while (j >= 0 && c.nodes[static_cast<std::size_t>(j)].t > key.t) {
            c.nodes[static_cast<std::size_t>(j + 1)] = c.nodes[static_cast<std::size_t>(j)];
            --j;
        }
        c.nodes[static_cast<std::size_t>(j + 1)] = key;
    }
    c.nodes[0].t = 0.0f;
    c.nodes[static_cast<std::size_t>(c.count - 1)].t = 1.0f;
}

inline int insertEnvelopeNode(EnvelopeCurve& c, float t, float y, float tension = -0.35f,
                              EnvSegment seg = EnvSegment::exp) noexcept HBA_NONBLOCKING {
    if (c.count < 2)
        sanitizeEnvelope(c);
    if (c.count >= kMaxEnvNodes)
        return -1;
    const float tc = std::isfinite(t) ? std::clamp(t, 0.005f, 0.995f) : 0.5f;
    const float yc = std::isfinite(y) ? std::clamp(y, 0.0f, 1.0f) : 0.5f;
    const float tens = std::isfinite(tension) ? std::clamp(tension, -1.0f, 1.0f) : 0.0f;
    c.nodes[static_cast<std::size_t>(c.count)] = {tc, yc, tens, seg};
    ++c.count;
    sanitizeEnvelope(c);
    int best = 0;
    float bestDist = 2.0f;
    for (int i = 0; i < c.count; ++i) {
        const float d = std::abs(c.nodes[static_cast<std::size_t>(i)].t - tc);
        if (d < bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

inline void moveEnvelopeNode(EnvelopeCurve& c, int index, float t, float y) noexcept HBA_NONBLOCKING {
    const int count = std::min(c.count, kMaxEnvNodes);
    if (index < 0 || index >= count)
        return;
    auto& n = c.nodes[static_cast<std::size_t>(index)];
    n.y = std::isfinite(y) ? std::clamp(y, 0.0f, 1.0f) : n.y;
    if (index == 0) {
        n.t = 0.0f;
    } else if (index == count - 1) {
        n.t = 1.0f;
    } else {
        const float prevT = c.nodes[static_cast<std::size_t>(index - 1)].t;
        const float nextT = c.nodes[static_cast<std::size_t>(index + 1)].t;
        const float lo = prevT + 0.004f;
        const float hi = nextT - 0.004f;
        const float safeT = std::isfinite(t) ? t : n.t;
        n.t = lo <= hi ? std::clamp(safeT, lo, hi) : 0.5f * (prevT + nextT);
    }
}

inline bool removeEnvelopeNode(EnvelopeCurve& c, int index) noexcept HBA_NONBLOCKING {
    if (c.count <= 2 || index <= 0 || index >= c.count - 1)
        return false;
    for (int i = index; i < c.count - 1; ++i)
        c.nodes[static_cast<std::size_t>(i)] = c.nodes[static_cast<std::size_t>(i + 1)];
    --c.count;
    return true;
}

// Canonical neurofunk kick pitch trajectory: snappy transient chirp -> chest knock -> locked sub.
// Normalized over [20 Hz .. 2400 Hz] log scale.
inline EnvelopeCurve defaultKickPitchCurve() noexcept HBA_NONBLOCKING {
    EnvelopeCurve c;
    c.count = 4;
    c.nodes[0] = {0.000f, 0.78f, -0.68f, EnvSegment::exp}; // ~860 Hz impact chirp
    c.nodes[1] = {0.065f, 0.34f, -0.42f, EnvSegment::exp}; // ~105 Hz chest knock
    c.nodes[2] = {0.260f, 0.20f, -0.25f, EnvSegment::exp}; // ~52 Hz fundamental settle
    c.nodes[3] = {1.000f, 0.19f, 0.00f, EnvSegment::line}; // steady locked sub floor
    return c;
}

// Canonical punchy amplitude envelope: instant onset, authoritative punch hold, tight neuro decay.
inline EnvelopeCurve defaultKickAmpCurve() noexcept HBA_NONBLOCKING {
    EnvelopeCurve c;
    c.count = 4;
    c.nodes[0] = {0.000f, 1.00f, 0.00f, EnvSegment::line};
    c.nodes[1] = {0.240f, 0.94f, -0.15f, EnvSegment::smooth};
    c.nodes[2] = {0.620f, 0.62f, -0.25f, EnvSegment::smooth};
    c.nodes[3] = {1.000f, 0.00f, 0.00f, EnvSegment::line};
    return c;
}

} // namespace hba::dsp
