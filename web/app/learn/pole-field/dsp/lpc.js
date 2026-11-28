// 14th-order Burg's Method LPC + Durand-Kerner complex root finder, mirroring products/pole-field/core/src/imprint_lpc.cpp.
import { FACTORY_CUBES, NUM_STAGES, SAMPLE_RATE } from "./cubes.js";
import { cAbs, cAdd, cArg, cDiv, cMul, cSub, clamp } from "./zplane.js";

export function runBurgLpcImprint(monoSamples, sampleRate = SAMPLE_RATE) {
  const fallback = FACTORY_CUBES[0].corners[0];
  if (!monoSamples || monoSamples.length < 128) return fallback;

  const decim = clamp(Math.round(sampleRate / 24000), 1, 8);
  const effFs = sampleRate / decim;
  const targetWin = clamp(Math.round(sampleRate * 0.0426), 128, 4096);
  const winLen = Math.min(targetWin, monoSamples.length);
  const hop = Math.max(1, Math.floor(winLen / 4));

  let bestStart = 0;
  let bestScore = 0;
  for (let start = 0; start + winLen <= monoSamples.length; start += hop) {
    let mean = 0;
    for (let i = 0; i < winLen; i++) mean += monoSamples[start + i];
    mean /= winLen;
    let energy = 0;
    let peak = 0;
    for (let i = 0; i < winLen; i++) {
      const v = monoSamples[start + i] - mean;
      energy += v * v;
      const av = Math.abs(v);
      if (av > peak) peak = av;
    }
    const rms = Math.sqrt(energy / winLen);
    if (rms < 1e-5) continue;
    const crest = peak / rms;
    const sustainWeight = 1 / (1 + Math.max(0, crest - 3.5) * 0.45);
    const score = rms * sustainWeight;
    if (score > bestScore) {
      bestScore = score;
      bestStart = start;
    }
  }

  let frameMean = 0;
  for (let i = 0; i < winLen; i++) frameMean += monoSamples[bestStart + i];
  frameMean /= winLen;

  const n = Math.floor(winLen / decim);
  if (n < 32) return fallback;

  const decimated = new Float64Array(n);
  let lp = 0;
  const alpha = decim > 1 ? 0.55 / decim : 1;
  for (let i = 0; i < n; i++) {
    let s = 0;
    for (let d = 0; d < decim; d++) {
      const raw = monoSamples[bestStart + i * decim + d] - frameMean;
      lp += alpha * (raw - lp);
      s = lp;
    }
    decimated[i] = decim > 1 ? s : monoSamples[bestStart + i] - frameMean;
  }

  const x = new Float64Array(n);
  let prev = 0;
  for (let i = 0; i < n; i++) {
    const pre = decimated[i] - 0.65 * prev;
    prev = decimated[i];
    const hann = 0.5 - 0.5 * Math.cos((2 * Math.PI * i) / (n - 1));
    x[i] = pre * hann;
  }

  const P = 14;
  const ef = Float64Array.from(x);
  const eb = Float64Array.from(x);
  let a = new Float64Array(P + 1);
  a[0] = 1;

  for (let m = 1; m <= P; m++) {
    let num = 0;
    let den = 0;
    for (let i = m; i < n; i++) {
      num += ef[i] * eb[i - 1];
      den += ef[i] * ef[i] + eb[i - 1] * eb[i - 1];
    }
    if (den < 1e-18) break;
    const km = clamp((-2 * num) / den, -0.992, 0.992);
    const nextA = Float64Array.from(a);
    for (let j = 1; j < m; j++) nextA[j] = a[j] + km * a[m - j];
    nextA[m] = km;
    a = nextA;

    for (let i = n - 1; i >= m; i--) {
      const f = ef[i] + km * eb[i - 1];
      const b = eb[i - 1] + km * ef[i];
      ef[i] = f;
      eb[i] = b;
    }
  }

  const roots = [];
  for (let i = 0; i < P; i++) {
    const ang = (2 * Math.PI * (i + 0.37)) / P;
    roots.push([0.82 * Math.cos(ang), 0.82 * Math.sin(ang)]);
  }
  const evalPoly = (z) => {
    let v = [1, 0];
    for (let j = 1; j <= P; j++) {
      v = cAdd(cMul(v, z), [a[j], 0]);
    }
    return v;
  };
  for (let iter = 0; iter < 100; iter++) {
    let maxStep = 0;
    for (let i = 0; i < P; i++) {
      const zi = roots[i];
      let denom = [1, 0];
      for (let j = 0; j < P; j++) {
        if (i === j) continue;
        denom = cMul(denom, cSub(zi, roots[j]));
      }
      if (cAbs(denom) < 1e-18) continue;
      const delta = cDiv(evalPoly(zi), denom);
      roots[i] = cSub(zi, delta);
      const step = cAbs(delta);
      if (step > maxStep) maxStep = step;
    }
    if (maxStep < 1e-9) break;
  }

  const poles = [];
  for (const r of roots) {
    if (r[1] <= 1e-3) continue;
    const radius = clamp(cAbs(r), 0.25, 0.992);
    const theta = cArg(r);
    if (theta < 0.012 || theta > Math.PI * 0.95) continue;
    const hz = clamp((theta * effFs) / (2 * Math.PI), 55, 11500);
    const bw = Math.max(18, (-Math.log(radius) * effFs) / Math.PI);
    const q = clamp(hz / bw, 1.4, 12.0);
    if (!poles.some((p) => Math.abs(p.hz - hz) / Math.max(p.hz, hz) < 0.06)) {
      poles.push({ hz, q });
    }
  }

  const gridHz = [180, 420, 920, 1950, 3800, 6800, 10200];
  for (const gHz of gridHz) {
    if (poles.length >= NUM_STAGES) break;
    if (!poles.some((p) => Math.abs(p.hz - gHz) / Math.max(p.hz, gHz) < 0.14)) {
      poles.push({ hz: gHz, q: 1.7 });
    }
  }
  poles.sort((u, v) => u.hz - v.hz);
  const finalPoles = poles.slice(0, NUM_STAGES);

  const stages = finalPoles.map((p, i) => {
    let zeroHz = (i & 1) === 0 ? p.hz * 1.28 : p.hz * 0.78;
    if ((i & 1) === 0 && i + 1 < NUM_STAGES) {
      const nextHz = finalPoles[i + 1].hz;
      if (nextHz > p.hz * 1.12) zeroHz = clamp(Math.sqrt(p.hz * nextHz), p.hz * 1.12, p.hz * 1.65);
    } else if ((i & 1) === 1 && i > 0) {
      const prevHz = finalPoles[i - 1].hz;
      if (p.hz > prevHz * 1.12) zeroHz = clamp(Math.sqrt(p.hz * prevHz), p.hz * 0.6, p.hz * 0.89);
    }
    return {
      poleHz: clamp(p.hz, 55, 14000),
      poleQ: clamp(p.q, 1.3, 11.5),
      zeroHz: clamp(zeroHz, 45, 16500),
      zeroQ: clamp(p.q * 0.78, 1.3, 9.0),
    };
  });

  return { makeupDb: 0.0, stages };
}

export function generateSyntheticImprintSource(kind, sampleRate = 24000) {
  const N = 2048;
  const out = new Float32Array(N);
  const profiles = {
    vowel: [
      [730, 8.5, 1.0],
      [1090, 9.2, 0.85],
      [2440, 10.0, 0.6],
      [3400, 8.0, 0.35],
    ],
    snare: [
      [215, 7.0, 1.0],
      [485, 9.5, 0.75],
      [1180, 11.0, 0.85],
      [2680, 9.0, 0.65],
      [4900, 8.0, 0.45],
    ],
    brass: [
      [340, 6.0, 0.9],
      [820, 8.5, 1.0],
      [1650, 9.0, 0.8],
      [3100, 7.5, 0.55],
      [5200, 6.0, 0.3],
    ],
    bell: [
      [440, 14.0, 0.9],
      [1215, 15.0, 1.0],
      [2390, 14.0, 0.85],
      [3980, 12.0, 0.65],
      [6450, 10.0, 0.4],
    ],
  };
  const modes = profiles[kind] || profiles.vowel;
  let seed = 1337;
  const rand = () => {
    seed = (seed * 1664525 + 1013904223) >>> 0;
    return seed / 4294967296 - 0.5;
  };
  const states = modes.map(() => ({ ic1: 0, ic2: 0 }));
  const f0 = kind === "snare" ? 185 : kind === "bell" ? 220 : 110;
  for (let i = 0; i < N; i++) {
    const ph = ((i * f0) / sampleRate) % 1;
    const exc = (ph < 0.08 ? 1 : -0.08) + (kind === "snare" ? rand() * 0.65 : rand() * 0.08);
    let y = 0;
    for (let m = 0; m < modes.length; m++) {
      const [hz, q, amp] = modes[m];
      const g = Math.tan((Math.PI * hz) / sampleRate);
      const k = 1 / q;
      const a1 = 1 / (1 + g * (g + k));
      const a2 = g * a1;
      const a3 = g * a2;
      const st = states[m];
      const v3 = exc - st.ic2;
      const v1 = a1 * st.ic1 + a2 * v3;
      const v2 = st.ic2 + a2 * st.ic1 + a3 * v3;
      st.ic1 = 2 * v1 - st.ic1;
      st.ic2 = 2 * v2 - st.ic2;
      y += amp * v1;
    }
    out[i] = y;
  }
  return { samples: out, sampleRate };
}
