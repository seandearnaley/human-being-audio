"use client";

import { STUDIO } from "../../../../lib/catalog.mjs";

export const REPO = STUDIO.repository;

export const RECIPES = [
  {
    name: "Liquid Neuro Chew",
    cubeIndex: 0,
    morphX: 0.22,
    morphY: 0.35,
    morphZ: 0.15,
    res: 0.56,
    topology: 0,
    stages: 7,
    drive: 0.42,
    drivePos: 1,
    source: "reese",
  },
  {
    name: "Talking Vowel Morph",
    cubeIndex: 1,
    morphX: 0.48,
    morphY: 0.18,
    morphZ: 0.25,
    res: 0.62,
    topology: 0,
    stages: 7,
    drive: 0.28,
    drivePos: 1,
    source: "reese",
  },
  {
    name: "Hollow Phase Razor",
    cubeIndex: 2,
    morphX: 0.68,
    morphY: 0.52,
    morphZ: 0.4,
    res: 0.48,
    topology: 1,
    stages: 7,
    drive: 0.35,
    drivePos: 1,
    source: "chord",
  },
  {
    name: "Parallel Formant Choir",
    cubeIndex: 1,
    morphX: 0.15,
    morphY: 0.72,
    morphZ: 0.1,
    res: 0.72,
    topology: 2,
    stages: 7,
    drive: 0.32,
    drivePos: 1,
    source: "drums",
  },
];

export const CPP_DIPOLE = `// products/pole-field/engine/src/pole_zero.h:27-42
static PoleZeroCoefs make(const SectionSpec& s, double sampleRate) noexcept PF_NONBLOCKING {
    const double gp = SvfCoefs::prewarp(static_cast<double>(s.poleHz), sampleRate);
    const double gz = SvfCoefs::prewarp(static_cast<double>(s.zeroHz), sampleRate);
    const double kp = 1.0 / std::clamp(static_cast<double>(s.poleQ), 0.5, 45.0);
    const double kz = 1.0 / std::clamp(static_cast<double>(s.zeroQ), 0.5, 24.0);
    const double r = gz / gp;
    const double norm = 1.0 / std::max(0.25, r);

    PoleZeroCoefs c;
    c.svf = SvfCoefs::fromGK(gp, kp);
    c.m0 = norm;
    c.m1 = norm * (r * kz - kp);
    c.m2 = norm * (r * r - 1.0);
    c.bpGain = std::sqrt(kp);
    return c;
}

// Nonlinear passive OTA resonance damping (pole_zero.h:84-92):
const double e = v1Lin * (1.0 / 3.2);
const double kAdd = 0.28 * (e * e) / (1.0 + e * e);
const double a1 = 1.0 / (1.0 + g * (g + c.svf.k + kAdd));`;

export const CPP_POLAR = `// products/pole-field/core/src/lattice_eval.cpp
for (std::size_t s = 0; s < kNumStages; ++s) {
    // Trilinear morph in log-frequency & log-Q keeps poles strictly inside |z| < 1
    const auto pf = logTrilerp(/* 8 cube corners poleHz */, x, y, z);
    const auto pq = logTrilerp(/* 8 cube corners poleQ  */, x, y, z);
    const auto zf = logTrilerp(/* 8 cube corners zeroHz */, x, y, z);
    const auto zq = logTrilerp(/* 8 cube corners zeroQ  */, x, y, z);

    // Alternating binaural stereo shift: even +st, odd -st
    const double stageStereoSt = ((s & 1u) == 0u) ? stereoOffsetSt : -stereoOffsetSt;
    const double freqMul = std::pow(2.0, (freqSemitones + stageStereoSt) / 12.0);
    const double ratio = zf / std::max(pf, 20.0);
    const double warpedRatio = std::pow(ratio, 1.0 + 0.85 * xform) * std::pow(2.0, 0.35 * xform);

    out.stages[s] = {
        clamp(pf * freqMul, 30.0, 18500.0),
        clamp(pq * resScale, 0.55, 45.0),
        clamp(finalPoleHz * warpedRatio, 30.0, 19500.0),
        clamp(zq * (0.55 + 0.75 * clampedRes), 0.55, 24.0)
    };
}`;

export const CPP_SUBLOCK = `// products/pole-field/engine/src/engine.cpp:35-40
// Exact 0.00 dB flat LR4 Sub Lock crossover + Dome Hilbert reference alignment:
//   Sub = subFit(LR4_LP(inMono))
//   Pre-lattice high = LR4_HP(in)  (-39 dB at f0/3 -> prevents drive IM in sub band)
//   Post-lattice Mid passes unfiltered at subSplit -> Sub + WetMid == exact 0.00 dB LR4_AP!
//   Post-lattice Side runs LR4_HP(subSplit) so sub band stays < -65 dB strictly mono!`;

export const CPP_ADAA = `// modules/dsp/include/hba/dsp/adaa.h:101-127 (TanhShaper ADAA2)
static double ad1(double u) noexcept HBA_DSP_NONBLOCKING {
    const double a = std::abs(u);
    if (a <= 0.5) { // Maclaurin polynomial eliminates cancellation near 0
        const double u2 = u * u;
        double p = 0.0;
        for (std::size_t i = kC1.size(); i-- > 0;) p = p * u2 + kC1[i];
        return u2 * p;
    }
    return a + std::log1p(std::exp(-2.0 * a)) - detail::kLn2;
}
static double ad2(double u) noexcept HBA_DSP_NONBLOCKING {
    const double a = std::abs(u);
    // Landen-transformed dilogarithm Li_2(-e^{-2|u|}) gives ~1e-15 seam continuity
    const double x = std::exp(-2.0 * a);
    const double lp = std::log1p(x);
    const double li2Neg = -detail::dilogPosHalf(x / (1.0 + x)) - 0.5 * lp * lp;
    const double val = 0.5 * a * a - detail::kLn2 * a + detail::kPi2Over24 + 0.5 * li2Neg;
    return u < 0.0 ? -val : val;
}`;

export const CPP_BURG = `// products/pole-field/core/src/imprint_lpc.cpp (14th-order Burg's Method LPC)
constexpr int P = 2 * kNumStages;  // order 14 -> 7 conjugate resonant formants
for (int m = 1; m <= P; ++m) {
    double num = 0.0, den = 0.0;
    for (std::size_t i = m; i < n; ++i) {
        num += ef[i] * eb[i - 1];
        den += ef[i] * ef[i] + eb[i - 1] * eb[i - 1];
    }
    // |km| < 1 mathematically guarantees ALL 14 poles stay inside the unit circle!
    const double km = std::clamp(-2.0 * num / den, -0.992, 0.992);
    for (int j = 1; j < m; ++j) nextA[j] = a[j] + km * a[m - j];
    nextA[m] = km;
    a = nextA;
    for (std::size_t i = n - 1; i >= m; --i) {
        const double f = ef[i] + km * eb[i - 1];
        const double b = eb[i - 1] + km * ef[i];
        ef[i] = f; eb[i] = b;
    }
}
// Then solve z^14 + a[1]z^13 + ... + a[14] = 0 via Durand-Kerner complex iteration`;

export function SliderControl({ id, label, value, min, max, step, displayValue, onChange }) {
  const styleVars = {
    "--attr-min": min,
    "--attr-max": max,
    "--control-value": value,
  };
  return (
    <div className="fg-slider-row">
      <div className="fg-slider-row__head">
        <label htmlFor={id}>{label}</label>
        <output htmlFor={id}>{displayValue}</output>
      </div>
      <input
        id={id}
        type="range"
        className="fg-slider"
        min={min}
        max={max}
        step={step}
        value={value}
        style={styleVars}
        aria-valuetext={displayValue}
        onChange={(e) => onChange(Number(e.target.value))}
      />
    </div>
  );
}

export function CodeExcerpt({ file, lines, code }) {
  const href = `${REPO}/blob/main/${file}#L${lines.replace("-", "-L")}`;
  return (
    <div className="fg-code-card">
      <div className="fg-code-card__bar">
        <span>
          {file} · L{lines}
        </span>
        <a href={href} target="_blank" rel="noreferrer">
          View exact C++ on GitHub ↗
        </a>
      </div>
      <pre>
        <code>{code}</code>
      </pre>
    </div>
  );
}
