"use client";

import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import Link from "next/link";
import studio from "../../../../studio.json";
import {
  FACTORY_CUBES,
  POLE_FIELD_WORKLET_CODE,
  SAMPLE_RATE,
  compareCartesianVsPolar,
  evaluateAdaaSpectrum,
  evaluateMagnitudeDb,
  evaluateStageH,
  generateSyntheticImprintSource,
  resolveLattice,
  runBurgLpcImprint,
  stageZPoint,
  zPointToHzQ,
} from "./dspMath";

const REPO = studio.repository;

const RECIPES = [
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

const CPP_DIPOLE = `// products/pole-field/engine/src/pole_zero.h:27-42
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

const CPP_POLAR = `// products/pole-field/core/src/cubes.cpp:1207-1237
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

const CPP_SUBLOCK = `// products/pole-field/engine/src/engine.cpp:35-40
// Exact 0.00 dB flat LR4 Sub Lock crossover + Dome Hilbert reference alignment:
//   Sub = subFit(LR4_LP(inMono))
//   Pre-lattice high = LR4_HP(in)  (-39 dB at f0/3 -> prevents drive IM in sub band)
//   Post-lattice Mid passes unfiltered at subSplit -> Sub + WetMid == exact 0.00 dB LR4_AP!
//   Post-lattice Side runs LR4_HP(subSplit) so sub band stays < -65 dB strictly mono!`;

const CPP_ADAA = `// modules/dsp/include/hba/dsp/adaa.h:101-127 (TanhShaper ADAA2)
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

const CPP_BURG = `// products/pole-field/core/src/cubes.cpp:1428-1455 (14th-order Burg's Method LPC)
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

function SliderControl({ id, label, value, min, max, step, displayValue, onChange }) {
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

function CodeExcerpt({ file, lines, code }) {
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

// Reusable canvas helper: draws a crisp Bode frequency response (20 Hz - 20 kHz, -24..+24 dB)
function drawBodeCanvas(canvas, evalDbAtHz, accentHex = "#e879c5", secondaryEval = null) {
  if (!canvas) return;
  const ctx = canvas.getContext("2d");
  const W = canvas.width;
  const H = canvas.height;
  ctx.clearRect(0, 0, W, H);

  const padL = 38;
  const padR = 14;
  const padT = 26;
  const padB = 24;
  const plotW = W - padL - padR;
  const plotH = H - padT - padB;

  const minLog = Math.log10(25);
  const maxLog = Math.log10(18000);
  const xOfHz = (hz) => padL + ((Math.log10(hz) - minLog) / (maxLog - minLog)) * plotW;
  const yOfDb = (db) => padT + ((24 - Math.max(-24, Math.min(24, db))) / 48) * plotH;

  // Grid
  ctx.strokeStyle = "rgba(255,255,255,0.08)";
  ctx.lineWidth = 1;
  for (const hz of [50, 100, 200, 500, 1000, 2000, 5000, 10000]) {
    const x = xOfHz(hz);
    ctx.beginPath();
    ctx.moveTo(x, padT);
    ctx.lineTo(x, padT + plotH);
    ctx.stroke();
    ctx.fillStyle = "rgba(205,212,232,0.52)";
    ctx.font = '10px "Martian Mono", monospace';
    ctx.textAlign = "center";
    ctx.fillText(hz >= 1000 ? `${hz / 1000}k` : `${hz}`, x, H - 7);
  }
  for (const db of [-18, -12, 0, 12, 18]) {
    const y = yOfDb(db);
    ctx.strokeStyle = db === 0 ? "rgba(255,255,255,0.22)" : "rgba(255,255,255,0.07)";
    ctx.beginPath();
    ctx.moveTo(padL, y);
    ctx.lineTo(padL + plotW, y);
    ctx.stroke();
    ctx.fillStyle = "rgba(205,212,232,0.52)";
    ctx.font = '9.5px "Martian Mono", monospace';
    ctx.textAlign = "right";
    ctx.fillText(db > 0 ? `+${db}` : `${db}`, padL - 6, y + 3);
  }

  // Optional comparison curve (e.g. Naive Cartesian)
  if (secondaryEval) {
    ctx.beginPath();
    for (let i = 0; i <= 160; i++) {
      const hz = Math.pow(10, minLog + (i / 160) * (maxLog - minLog));
      const x = padL + (i / 160) * plotW;
      const y = yOfDb(secondaryEval(hz));
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    }
    ctx.strokeStyle = "rgba(255, 118, 118, 0.75)";
    ctx.lineWidth = 1.8;
    ctx.setLineDash([5, 4]);
    ctx.stroke();
    ctx.setLineDash([]);
  }

  // Primary curve + soft fill
  ctx.beginPath();
  for (let i = 0; i <= 180; i++) {
    const hz = Math.pow(10, minLog + (i / 180) * (maxLog - minLog));
    const x = padL + (i / 180) * plotW;
    const y = yOfDb(evalDbAtHz(hz));
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  }
  ctx.strokeStyle = accentHex;
  ctx.lineWidth = 2.4;
  ctx.stroke();

  const zeroY = yOfDb(0);
  ctx.lineTo(padL + plotW, zeroY);
  ctx.lineTo(padL, zeroY);
  ctx.closePath();
  ctx.fillStyle = "rgba(232, 121, 197, 0.14)";
  ctx.fill();
}

// Reusable canvas helper: draws a glowing Z-plane unit circle with conjugate poles (X) & zeros (O)
function drawZPlaneCanvas(canvas, stages, activeStages = 7, highlightIdx = -1) {
  if (!canvas) return;
  const ctx = canvas.getContext("2d");
  const W = canvas.width;
  const H = canvas.height;
  ctx.clearRect(0, 0, W, H);

  const cx = W * 0.5;
  const cy = H * 0.56;
  const R = Math.min(W * 0.32, H * 0.35);

  // Stability interior glow
  const grad = ctx.createRadialGradient(cx, cy, R * 0.1, cx, cy, R);
  grad.addColorStop(0, "rgba(56, 217, 169, 0.04)");
  grad.addColorStop(0.82, "rgba(232, 121, 197, 0.08)");
  grad.addColorStop(1, "rgba(255, 107, 107, 0.16)");
  ctx.fillStyle = grad;
  ctx.beginPath();
  ctx.arc(cx, cy, R, 0, Math.PI * 2);
  ctx.fill();

  // Concentric reference rings + axes
  ctx.strokeStyle = "rgba(255,255,255,0.10)";
  ctx.lineWidth = 1;
  for (const frac of [0.35, 0.7]) {
    ctx.beginPath();
    ctx.arc(cx, cy, R * frac, 0, Math.PI * 2);
    ctx.stroke();
  }
  ctx.beginPath();
  ctx.moveTo(cx - R - 10, cy);
  ctx.lineTo(cx + R + 10, cy);
  ctx.moveTo(cx, cy - R - 10);
  ctx.lineTo(cx, cy + R + 10);
  ctx.stroke();

  // Unit circle |z| = 1 boundary
  ctx.strokeStyle = "rgba(255,255,255,0.42)";
  ctx.lineWidth = 1.6;
  ctx.beginPath();
  ctx.arc(cx, cy, R, 0, Math.PI * 2);
  ctx.stroke();

  // Frequency landmarks
  ctx.fillStyle = "rgba(205,212,232,0.52)";
  ctx.font = '9.5px "Martian Mono", monospace';
  ctx.textAlign = "left";
  ctx.fillText("0 Hz", cx + R + 5, cy + 3);
  ctx.textAlign = "right";
  ctx.fillText("Nyq", cx - R - 5, cy + 3);
  ctx.textAlign = "center";
  ctx.fillText("fs/4", cx, cy - R - 5);

  const count = Math.min(activeStages, stages.length);
  for (let i = 0; i < count; i++) {
    const pt = stageZPoint(stages[i], SAMPLE_RATE);
    const isHi = i === highlightIdx;
    // Draw both upper (+theta) and conjugate (-theta) pairs
    for (const sign of [1, -1]) {
      const px = cx + pt.poleR * R * Math.cos(pt.poleTheta);
      const py = cy - sign * pt.poleR * R * Math.sin(pt.poleTheta);
      const zx = cx + pt.zeroR * R * Math.cos(pt.zeroTheta);
      const zy = cy - sign * pt.zeroR * R * Math.sin(pt.zeroTheta);

      // Dipole connector tether
      ctx.strokeStyle = sign > 0 ? "rgba(232,121,197,0.34)" : "rgba(232,121,197,0.14)";
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(px, py);
      ctx.lineTo(zx, zy);
      ctx.stroke();

      // Zero circle (cyan)
      ctx.strokeStyle = sign > 0 ? "#66d9e8" : "rgba(102,217,232,0.4)";
      ctx.lineWidth = isHi ? 2.4 : 1.7;
      ctx.beginPath();
      ctx.arc(zx, zy, isHi ? 6.5 : 4.8, 0, Math.PI * 2);
      ctx.stroke();

      // Pole X (magenta)
      const s = isHi ? 6.5 : 4.8;
      ctx.strokeStyle = sign > 0 ? "#f783ac" : "rgba(247,131,172,0.45)";
      ctx.lineWidth = isHi ? 2.6 : 1.9;
      ctx.beginPath();
      ctx.moveTo(px - s, py - s);
      ctx.lineTo(px + s, py + s);
      ctx.moveTo(px + s, py - s);
      ctx.lineTo(px - s, py + s);
      ctx.stroke();
    }
  }
}

export default function PoleFieldGuide() {
  // Global depth & live audio state
  const [depth, setDepth] = useState("both"); // "musician" | "both" | "engineer"
  const [activeAudioLab, setActiveAudioLab] = useState(null); // null | "lab1" | "lab3" | "lab4" | "lab5"
  const [audioStatus, setAudioStatus] = useState("Idle");

  // AudioWorklet engine refs
  const audioCtxRef = useRef(null);
  const workletRef = useRef(null);

  // Lab 1: Single Dipole Z-Plane Playground
  const [dipole, setDipole] = useState({ poleHz: 780, poleQ: 7.5, zeroHz: 1450, zeroQ: 5.5 });
  const [dragTarget, setDragTarget] = useState(null); // "pole" | "zero" | null
  const lab1ZRef = useRef(null);
  const lab1BodeRef = useRef(null);

  // Lab 2: Polar vs Naive Cartesian Stability Lab
  const [morphT, setMorphT] = useState(0.5);
  const lab2ZRef = useRef(null);
  const lab2BodeRef = useRef(null);

  // Lab 3: Full 14-Pole Morph Cube Bench
  const [cubeIndex, setCubeIndex] = useState(0);
  const [morphX, setMorphX] = useState(0.24);
  const [morphY, setMorphY] = useState(0.32);
  const [morphZ, setMorphZ] = useState(0.15);
  const [res, setRes] = useState(0.55);
  const [topology, setTopology] = useState(0); // 0 serial, 1 hybrid, 2 parallel
  const [stagesCount, setStagesCount] = useState(7);
  const [drive, setDrive] = useState(0.38);
  const [drivePos, setDrivePos] = useState(1); // 0 pre, 1 inter, 2 post
  const [subLock, setSubLock] = useState(true);
  const [audioSource, setAudioSource] = useState("reese");
  const lab3PadRef = useRef(null);
  const lab3ZRef = useRef(null);
  const lab3BodeRef = useRef(null);
  const padDraggingRef = useRef(false);

  // Lab 4: ADAA2 Anti-Aliasing Lab
  const [adaaDrive, setAdaaDrive] = useState(5.5);
  const [adaaFreq, setAdaaFreq] = useState(2130);
  const [adaaShaper, setAdaaShaper] = useState("buchla");
  const [adaaOs, setAdaaOs] = useState(2);
  const [adaaEnabled, setAdaaEnabled] = useState(true);
  const lab4SpecRef = useRef(null);

  // Lab 5: 14th-Order Burg LPC Imprinting Lab
  const [imprintKind, setImprintKind] = useState("vowel");
  const [imprintLabel, setImprintLabel] = useState("Human 'Ah' Vowel (built-in)");
  const [imprintCorner, setImprintCorner] = useState(() => {
    const src = generateSyntheticImprintSource("vowel");
    return runBurgLpcImprint(src.samples, src.sampleRate);
  });
  const lab5ZRef = useRef(null);
  const lab5BodeRef = useRef(null);

  // Computed lattices
  const lab3Lattice = useMemo(
    () =>
      resolveLattice({
        cubeIndex,
        morphX,
        morphY,
        morphZ,
        resAmount: res,
        activeStages: stagesCount,
        topology,
      }),
    [cubeIndex, morphX, morphY, morphZ, res, stagesCount, topology]
  );

  const lab5Lattice = useMemo(
    () =>
      resolveLattice({
        cubeIndex: 0,
        resAmount: 0.52,
        activeStages: 7,
        topology: 0,
        customCorner: imprintCorner,
      }),
    [imprintCorner]
  );

  const lab2Data = useMemo(() => compareCartesianVsPolar(morphT), [morphT]);
  const lab4Data = useMemo(
    () => evaluateAdaaSpectrum(adaaDrive, adaaFreq, adaaShaper, adaaOs),
    [adaaDrive, adaaFreq, adaaShaper, adaaOs]
  );

  // Initialize AudioWorklet on first user Play click
  const ensureAudioEngine = useCallback(async () => {
    if (workletRef.current && audioCtxRef.current) {
      if (audioCtxRef.current.state === "suspended") {
        await audioCtxRef.current.resume();
      }
      return workletRef.current;
    }
    const Ctx = window.AudioContext || window.webkitAudioContext;
    if (!Ctx) {
      setAudioStatus("Web Audio unavailable");
      return null;
    }
    const ctx = new Ctx({ sampleRate: 48000 });
    audioCtxRef.current = ctx;
    if (ctx.audioWorklet) {
      const blob = new Blob([POLE_FIELD_WORKLET_CODE], { type: "application/javascript" });
      const url = URL.createObjectURL(blob);
      await ctx.audioWorklet.addModule(url);
      URL.revokeObjectURL(url);
      const node = new AudioWorkletNode(ctx, "pole-field-bench", {
        numberOfInputs: 0,
        numberOfOutputs: 1,
        outputChannelCount: [2],
      });
      node.connect(ctx.destination);
      workletRef.current = node;
      return node;
    }
    setAudioStatus("AudioWorklet unsupported");
    return null;
  }, []);

  const stopAllAudio = useCallback(() => {
    setActiveAudioLab(null);
    setAudioStatus("Idle");
    if (workletRef.current) {
      workletRef.current.port.postMessage({ type: "state", playing: false });
    }
  }, []);

  const toggleLabAudio = useCallback(
    async (labId) => {
      if (activeAudioLab === labId) {
        stopAllAudio();
        return;
      }
      try {
        const node = await ensureAudioEngine();
        if (!node) return;
        setActiveAudioLab(labId);
        setAudioStatus(
          labId === "lab1"
            ? "Playing Lab 1 Dipole"
            : labId === "lab3"
              ? "Playing 14-Pole Lattice"
              : labId === "lab4"
                ? "Playing ADAA2 Tone"
                : "Playing Burg LPC Imprint"
        );
      } catch {
        setAudioStatus("Audio blocked");
      }
    },
    [activeAudioLab, ensureAudioEngine, stopAllAudio]
  );

  // Clean up audio context on unmount
  useEffect(() => {
    return () => {
      if (audioCtxRef.current) {
        audioCtxRef.current.close().catch(() => {});
      }
    };
  }, []);

  // Push live DSP state into the AudioWorklet whenever any control moves
  useEffect(() => {
    const node = workletRef.current;
    if (!node) return;
    if (!activeAudioLab) {
      node.port.postMessage({ type: "state", playing: false });
      return;
    }
    if (activeAudioLab === "lab1") {
      node.port.postMessage({
        type: "state",
        playing: true,
        mode: "single",
        source: "reese",
        drive: 0.15,
        drivePos: 2,
        subLock: false,
        makeup: 0.9,
        stages: [dipole],
      });
    } else if (activeAudioLab === "lab3") {
      node.port.postMessage({
        type: "state",
        playing: true,
        mode: "lattice",
        source: audioSource,
        topology,
        activeStages: stagesCount,
        drive,
        drivePos,
        subLock,
        makeup: lab3Lattice.makeupLinear,
        stages: lab3Lattice.stages,
      });
    } else if (activeAudioLab === "lab4") {
      node.port.postMessage({
        type: "state",
        playing: true,
        mode: "adaa",
        adaaEnabled,
        adaaDrive,
        adaaFreq,
        adaaShaper,
      });
    } else if (activeAudioLab === "lab5") {
      node.port.postMessage({
        type: "state",
        playing: true,
        mode: "lattice",
        source: "reese",
        topology: 0,
        activeStages: 7,
        drive: 0.28,
        drivePos: 1,
        subLock: true,
        makeup: lab5Lattice.makeupLinear,
        stages: lab5Lattice.stages,
      });
    }
  }, [
    activeAudioLab,
    dipole,
    audioSource,
    topology,
    stagesCount,
    drive,
    drivePos,
    subLock,
    lab3Lattice,
    adaaEnabled,
    adaaDrive,
    adaaFreq,
    adaaShaper,
    lab5Lattice,
  ]);

  // Draw Lab 1 canvases
  useEffect(() => {
    drawZPlaneCanvas(lab1ZRef.current, [dipole], 1, 0);
    drawBodeCanvas(lab1BodeRef.current, (hz) => {
      const h = evaluateStageH(dipole, hz, SAMPLE_RATE);
      return 20 * Math.log10(Math.max(Math.hypot(h[0], h[1]), 1e-4));
    });
  }, [dipole]);

  // Pointer drag interaction for Lab 1 Z-plane unit circle
  const handleLab1Pointer = useCallback((e, isDown) => {
    const canvas = lab1ZRef.current;
    if (!canvas) return;
    const rect = canvas.getBoundingClientRect();
    const x = ((e.clientX - rect.left) / rect.width) * canvas.width;
    const y = ((e.clientY - rect.top) / rect.height) * canvas.height;
    const cx = canvas.width * 0.5;
    const cy = canvas.height * 0.56;
    const R = Math.min(canvas.width * 0.32, canvas.height * 0.35);

    const dx = (x - cx) / R;
    const dy = -(y - cy) / R;
    const r = Math.min(0.992, Math.max(0.12, Math.hypot(dx, dy)));
    const theta = Math.max(0.03, Math.min(Math.PI - 0.05, Math.abs(Math.atan2(dy, dx))));

    let target = dragTarget;
    if (isDown) {
      const pt = stageZPoint(dipole, SAMPLE_RATE);
      const px = cx + pt.poleR * R * Math.cos(pt.poleTheta);
      const py = cy - pt.poleR * R * Math.sin(pt.poleTheta);
      const zx = cx + pt.zeroR * R * Math.cos(pt.zeroTheta);
      const zy = cy - pt.zeroR * R * Math.sin(pt.zeroTheta);
      const dPole = Math.hypot(x - px, y - py);
      const dZero = Math.hypot(x - zx, y - zy);
      target = dZero < dPole ? "zero" : "pole";
      setDragTarget(target);
      canvas.setPointerCapture?.(e.pointerId);
    }
    if (!target) return;
    const { hz, q } = zPointToHzQ(r, theta, SAMPLE_RATE);
    setDipole((prev) =>
      target === "pole"
        ? { ...prev, poleHz: Math.round(hz), poleQ: Number(q.toFixed(1)) }
        : { ...prev, zeroHz: Math.round(hz), zeroQ: Number(Math.min(24, q).toFixed(1)) }
    );
  }, [dipole, dragTarget]);

  // Draw Lab 2 canvases (Log-Polar arc vs Direct-Form chord)
  useEffect(() => {
    const canvas = lab2ZRef.current;
    if (canvas) {
      const ctx = canvas.getContext("2d");
      const W = canvas.width;
      const H = canvas.height;
      ctx.clearRect(0, 0, W, H);
      const cx = W * 0.5;
      const cy = H * 0.53;
      const R = Math.min(W, H) * 0.39;

      // Unit circle
      ctx.strokeStyle = "rgba(255,255,255,0.38)";
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      ctx.arc(cx, cy, R, 0, Math.PI * 2);
      ctx.stroke();

      // Axes
      ctx.strokeStyle = "rgba(255,255,255,0.1)";
      ctx.beginPath();
      ctx.moveTo(cx - R, cy);
      ctx.lineTo(cx + R, cy);
      ctx.moveTo(cx, cy - R);
      ctx.lineTo(cx, cy + R);
      ctx.stroke();

      // Trace both full trajectories (0..1)
      ctx.beginPath();
      for (let i = 0; i <= 60; i++) {
        const d = compareCartesianVsPolar(i / 60).polar;
        const x = cx + d.r * R * Math.cos(d.theta);
        const y = cy - d.r * R * Math.sin(d.theta);
        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
      }
      ctx.strokeStyle = "#38d9a9";
      ctx.lineWidth = 2.4;
      ctx.stroke();

      ctx.beginPath();
      for (let i = 0; i <= 60; i++) {
        const d = compareCartesianVsPolar(i / 60).cartesian;
        const x = cx + d.r * R * Math.cos(d.theta);
        const y = cy - d.r * R * Math.sin(d.theta);
        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
      }
      ctx.strokeStyle = "#ff6b6b";
      ctx.lineWidth = 2;
      ctx.setLineDash([5, 4]);
      ctx.stroke();
      ctx.setLineDash([]);

      // Current markers at morphT
      const drawDot = (pt, color, label) => {
        const x = cx + pt.r * R * Math.cos(pt.theta);
        const y = cy - pt.r * R * Math.sin(pt.theta);
        ctx.fillStyle = color;
        ctx.beginPath();
        ctx.arc(x, y, 6, 0, Math.PI * 2);
        ctx.fill();
        ctx.fillStyle = "#fff";
        ctx.font = '10px "Martian Mono", monospace';
        ctx.fillText(label, x + 9, y + 3);
      };
      drawDot(lab2Data.polar, "#38d9a9", "Log-Polar");
      drawDot(lab2Data.cartesian, "#ff6b6b", "Cartesian");
    }

    drawBodeCanvas(
      lab2BodeRef.current,
      (hz) => {
        const st = {
          poleHz: lab2Data.polar.hz,
          poleQ: lab2Data.polar.q,
          zeroHz: lab2Data.polar.hz * 0.45,
          zeroQ: 1.2,
        };
        const h = evaluateStageH(st, hz, SAMPLE_RATE);
        return 20 * Math.log10(Math.max(Math.hypot(h[0], h[1]), 1e-4));
      },
      "#38d9a9",
      (hz) => {
        const st = {
          poleHz: lab2Data.cartesian.hz,
          poleQ: lab2Data.cartesian.q,
          zeroHz: lab2Data.cartesian.hz * 0.45,
          zeroQ: 1.2,
        };
        const h = evaluateStageH(st, hz, SAMPLE_RATE);
        return 20 * Math.log10(Math.max(Math.hypot(h[0], h[1]), 1e-4));
      }
    );
  }, [lab2Data]);

  // Draw Lab 3 canvases (XY Morph Pad + 14-Pole Unit Circle + 14-Pole Bode)
  useEffect(() => {
    const pad = lab3PadRef.current;
    if (pad) {
      const ctx = pad.getContext("2d");
      const W = pad.width;
      const H = pad.height;
      ctx.clearRect(0, 0, W, H);
      const cube = FACTORY_CUBES[cubeIndex];

      // Subtle grid
      ctx.strokeStyle = "rgba(255,255,255,0.08)";
      ctx.lineWidth = 1;
      for (let i = 1; i < 4; i++) {
        ctx.beginPath();
        ctx.moveTo((W * i) / 4, 0);
        ctx.lineTo((W * i) / 4, H);
        ctx.moveTo(0, (H * i) / 4);
        ctx.lineTo(W, (H * i) / 4);
        ctx.stroke();
      }

      // Corner labels (front or back face depending on Z) — positioned below the top-left overlay header
      const faceOffset = morphZ >= 0.5 ? 4 : 0;
      ctx.fillStyle = "rgba(220,226,245,0.78)";
      ctx.font = '10.5px "Martian Mono", monospace';
      ctx.textAlign = "left";
      ctx.fillText(cube.cornerNames[faceOffset + 0], 10, 38);
      ctx.fillText(cube.cornerNames[faceOffset + 2], 10, H - 10);
      ctx.textAlign = "right";
      ctx.fillText(cube.cornerNames[faceOffset + 1], W - 10, 38);
      ctx.fillText(cube.cornerNames[faceOffset + 3], W - 10, H - 10);

      // Cursor puck + crosshairs
      const px = 14 + morphX * (W - 28);
      const py = 44 + morphY * (H - 64);
      ctx.strokeStyle = "rgba(232,121,197,0.35)";
      ctx.beginPath();
      ctx.moveTo(px, 0);
      ctx.lineTo(px, H);
      ctx.moveTo(0, py);
      ctx.lineTo(W, py);
      ctx.stroke();

      const glow = ctx.createRadialGradient(px, py, 2, px, py, 28);
      glow.addColorStop(0, "rgba(232,121,197,0.75)");
      glow.addColorStop(1, "rgba(232,121,197,0)");
      ctx.fillStyle = glow;
      ctx.beginPath();
      ctx.arc(px, py, 28, 0, Math.PI * 2);
      ctx.fill();

      ctx.fillStyle = "#ffffff";
      ctx.strokeStyle = "#e879c5";
      ctx.lineWidth = 2.5;
      ctx.beginPath();
      ctx.arc(px, py, 7, 0, Math.PI * 2);
      ctx.fill();
      ctx.stroke();
    }

    drawZPlaneCanvas(lab3ZRef.current, lab3Lattice.stages, stagesCount);
    drawBodeCanvas(lab3BodeRef.current, (hz) =>
      evaluateMagnitudeDb(lab3Lattice, stagesCount, topology, hz, SAMPLE_RATE)
    );
  }, [cubeIndex, morphX, morphY, morphZ, lab3Lattice, stagesCount, topology]);

  const handlePadPointer = useCallback((e, isDown) => {
    const pad = lab3PadRef.current;
    if (!pad) return;
    if (isDown) {
      padDraggingRef.current = true;
      pad.setPointerCapture?.(e.pointerId);
    }
    if (!padDraggingRef.current) return;
    const rect = pad.getBoundingClientRect();
    const nx = Math.max(0, Math.min(1, (e.clientX - rect.left) / rect.width));
    const ny = Math.max(0, Math.min(1, (e.clientY - rect.top) / rect.height));
    setMorphX(Number(nx.toFixed(3)));
    setMorphY(Number(ny.toFixed(3)));
  }, []);

  // Draw Lab 4 ADAA2 Aliasing Spectrum
  useEffect(() => {
    const canvas = lab4SpecRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    const W = canvas.width;
    const H = canvas.height;
    ctx.clearRect(0, 0, W, H);

    const padL = 40;
    const padR = 14;
    const padT = 26;
    const padB = 24;
    const plotW = W - padL - padR;
    const plotH = H - padT - padB;
    const bins = lab4Data.naive.length;
    const yOfDb = (db) => padT + ((0 - Math.max(-96, Math.min(0, db))) / 96) * plotH;

    // Grid
    ctx.strokeStyle = "rgba(255,255,255,0.08)";
    for (const db of [0, -24, -48, -72, -96]) {
      const y = yOfDb(db);
      ctx.beginPath();
      ctx.moveTo(padL, y);
      ctx.lineTo(padL + plotW, y);
      ctx.stroke();
      ctx.fillStyle = "rgba(205,212,232,0.55)";
      ctx.font = '9.5px "Martian Mono", monospace';
      ctx.textAlign = "right";
      ctx.fillText(`${db}`, padL - 6, y + 3);
    }
    for (const khz of [2, 6, 10, 14, 18, 22]) {
      const x = padL + ((khz * 1000) / 24000) * plotW;
      ctx.beginPath();
      ctx.moveTo(x, padT);
      ctx.lineTo(x, padT + plotH);
      ctx.stroke();
      ctx.fillStyle = "rgba(205,212,232,0.55)";
      ctx.textAlign = "center";
      ctx.fillText(`${khz}k`, x, H - 7);
    }

    // Naive spectrum (crimson)
    ctx.beginPath();
    for (let i = 1; i < bins; i++) {
      const x = padL + (i / bins) * plotW;
      const y = yOfDb(lab4Data.naive[i]);
      if (i === 1) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    }
    ctx.strokeStyle = "rgba(255, 107, 107, 0.85)";
    ctx.lineWidth = 1.6;
    ctx.stroke();

    // ADAA spectrum (emerald/cyan)
    ctx.beginPath();
    for (let i = 1; i < bins; i++) {
      const x = padL + (i / bins) * plotW;
      const y = yOfDb(lab4Data.adaa[i]);
      if (i === 1) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    }
    ctx.strokeStyle = "#38d9a9";
    ctx.lineWidth = 2.0;
    ctx.stroke();
  }, [lab4Data]);

  // Draw Lab 5 Burg LPC Imprint canvases
  useEffect(() => {
    drawZPlaneCanvas(lab5ZRef.current, lab5Lattice.stages, 7);
    drawBodeCanvas(
      lab5BodeRef.current,
      (hz) => evaluateMagnitudeDb(lab5Lattice, 7, 0, hz, SAMPLE_RATE),
      "#66d9e8"
    );
  }, [lab5Lattice]);

  const handleSelectSyntheticImprint = useCallback((kind, label) => {
    setImprintKind(kind);
    setImprintLabel(label);
    const src = generateSyntheticImprintSource(kind);
    setImprintCorner(runBurgLpcImprint(src.samples, src.sampleRate));
  }, []);

  const handleAudioUpload = useCallback(async (e) => {
    const file = e.target.files?.[0];
    if (!file) return;
    try {
      const buf = await file.arrayBuffer();
      const Ctx = window.AudioContext || window.webkitAudioContext;
      const ctx = new Ctx();
      const decoded = await ctx.decodeAudioData(buf);
      const ch0 = decoded.getChannelData(0);
      const corner = runBurgLpcImprint(ch0, decoded.sampleRate);
      setImprintKind("upload");
      setImprintLabel(`Imprinted: ${file.name} (${decoded.sampleRate} Hz)`);
      setImprintCorner(corner);
      ctx.close().catch(() => {});
    } catch {
      setImprintLabel("Could not decode audio file — try WAV, MP3, or FLAC.");
    }
  }, []);

  const showMusician = depth === "musician" || depth === "both";
  const showEngineer = depth === "engineer" || depth === "both";
  const lab1Pt = stageZPoint(dipole, SAMPLE_RATE);

  return (
    <main id="main" className="fg-main" style={{ "--hue": 318 }}>
      {/* Hero */}
      <section className="fg-hero">
        <div className="wrap fg-hero__grid">
          <div>
            <p className="fg-eyebrow">
              <Link href="/learn">DSP Field Guides</Link> · Chapter 01 · Pole Field
            </p>
            <h1 className="fg-title">Painting With Poles.</h1>
            <p className="fg-lede">
              An interactive guide to <strong>14-pole Z-plane morphing filters</strong>, passive
              nonlinear resonance, anti-aliased inter-stage drive, and 14th-order Burg LPC audio
              imprinting. Play with live Web Audio models right in your browser, then trace every
              concept down to the real shipping C++20 engine.
            </p>
          </div>
          <aside className="fg-manifesto" aria-label="How to read this guide">
            <p className="fg-eyebrow" style={{ color: "oklch(0.82 0.14 318)" }}>
              Two Layers · One Instrument
            </p>
            <p>
              <strong>Layer 1 (Musician):</strong> Plain English, musical intuition, and playable
              live synth benches. Learn <em>why</em> a Z-plane filter talks like a vocal tract and
              chews through neuro bass.
            </p>
            <p>
              <strong>Layer 2 (DSP Engineer):</strong> Complex roots on the unit circle, Cytomic TPT
              equations, ADAA2 antiderivatives, and line-linked C++20 excerpts straight from{" "}
              <code>products/pole-field/</code>.
            </p>
          </aside>
        </div>
      </section>

      {/* Sticky Reading Depth & Master Audio Kill Switch */}
      <div className="fg-toolbar" role="region" aria-label="Guide reading depth and audio control">
        <div className="wrap fg-toolbar__inner">
          <div className="fg-depth-group" role="group" aria-label="Reading depth">
            {[
              ["musician", "Musician"],
              ["both", "Both Layers"],
              ["engineer", "DSP Engineer"],
            ].map(([id, label]) => (
              <button
                key={id}
                type="button"
                className="fg-depth-btn"
                aria-pressed={depth === id}
                onClick={() => setDepth(id)}
              >
                {label}
              </button>
            ))}
          </div>

          <button
            type="button"
            className={`fg-audio-pill${activeAudioLab ? " fg-audio-pill--on" : ""}`}
            onClick={() => (activeAudioLab ? stopAllAudio() : toggleLabAudio("lab3"))}
          >
            <span className="fg-led" aria-hidden="true" />
            <span>{activeAudioLab ? `Stop Audio (${audioStatus})` : "Audition Pole Field"}</span>
          </button>
        </div>
      </div>

      <div className="wrap">
        {/* ============================================================
            LAB 1: TOUCH THE Z-PLANE UNIT CIRCLE
           ============================================================ */}
        <section className="fg-chapter" id="unit-circle" aria-labelledby="ch1-title">
          <div className="fg-chapter__head">
            <span className="fg-chapter__num">01 · Interactive Intuition</span>
            <h2 id="ch1-title">Touch the Z-Plane: Poles Push Up, Zeros Pin Down</h2>
          </div>

          <div className="fg-layers" data-depth={depth}>
            {showMusician && (
              <article className="fg-layer fg-layer--musician">
                <span className="fg-layer__tag">Layer 1 · The Musical Ear</span>
                <p>
                  Picture a taut rubber sheet stretched over a circle. Every frequency from sub-bass
                  (right edge, <code>0 Hz</code>) to ultra-highs (left edge, Nyquist) wraps around
                  the rim.
                </p>
                <p>
                  A <strong>Pole (×)</strong> is a tent pole pushing the sheet upward into a singing
                  resonant formant. Push it closer to the outer rim and the peak gets sharper until
                  it whistles. A <strong>Zero (○)</strong> is a tent stake pinning the sheet down
                  into a deep phaser notch. Pair them side by side — a <strong>dipole</strong> — and
                  you get that liquid, talking throat bite that ordinary low-pass filters can never
                  produce.
                </p>
              </article>
            )}
            {showEngineer && (
              <article className="fg-layer fg-layer--engineer">
                <span className="fg-layer__tag">Layer 2 · Under the Hood (TPT Cytomic SVF)</span>
                <p>
                  Each stage in Pole Field is a 2nd-order continuous-time pole-zero section mapped
                  via the bilinear transform <code>z = (1 + s_c) / (1 − s_c)</code>, evaluated in
                  Topology-Preserving Transform (Cytomic SVF) form so modulation stays click-free at
                  audio rate.
                </p>
                <div className="fg-formula">
                  H(s) = norm · (s² + r·k_z·s + r²) / (s² + k_p·s + 1),&nbsp;&nbsp;r = tan(πf_z/f_s)
                  / tan(πf_p/f_s)
                </div>
                <p>
                  To let resonance sing cleanly up to <code>Q = 45</code> without numeric blowups or
                  harsh digital limit cycles, Pole Field injects <strong>passive nonlinear OTA
                  damping</strong> directly into the feedback term{" "}
                  <code>k_add = 0.28 · e² / (1 + e²)</code>.
                </p>
              </article>
            )}
          </div>

          <div className="fg-bench" aria-label="Single dipole Z-plane playground">
            <div className="fg-bench__bar">
              <span className="fg-bench__title">Lab 1 · Drag Pole (×) or Zero (○) Inside the Circle</span>
              <div className="fg-chips">
                <button
                  type="button"
                  className="fg-chip fg-chip--play"
                  aria-pressed={activeAudioLab === "lab1"}
                  onClick={() => toggleLabAudio("lab1")}
                >
                  {activeAudioLab === "lab1" ? "■ Stop Reese" : "▶ Play Live Dipole"}
                </button>
                <button
                  type="button"
                  className="fg-chip"
                  onClick={() => setDipole({ poleHz: 780, poleQ: 7.5, zeroHz: 1450, zeroQ: 5.5 })}
                >
                  Reset Dipole
                </button>
              </div>
            </div>

            <div className="fg-bench__body fg-bench__body--wide">
              <div className="fg-bench__stage">
                <div className="fg-canvas-pair">
                  <div className="fg-canvas-box fg-canvas-box--interactive">
                    <span className="fg-canvas-label">Z-Plane Unit Circle (drag × / ○)</span>
                    <canvas
                      ref={lab1ZRef}
                      width={320}
                      height={260}
                      aria-label="Interactive Z-plane unit circle; drag pole or zero"
                      onPointerDown={(e) => handleLab1Pointer(e, true)}
                      onPointerMove={(e) => handleLab1Pointer(e, false)}
                      onPointerUp={() => setDragTarget(null)}
                      onPointerCancel={() => setDragTarget(null)}
                    />
                  </div>
                  <div className="fg-canvas-box">
                    <span className="fg-canvas-label">Magnitude Response (25 Hz – 18 kHz)</span>
                    <canvas
                      ref={lab1BodeRef}
                      width={420}
                      height={260}
                      aria-label="Live dipole frequency response curve"
                    />
                  </div>
                </div>

                <div className="fg-readout">
                  <div className="fg-stat">
                    <span className="fg-stat__k">Pole Radius |z_p|</span>
                    <span className="fg-stat__v">{lab1Pt.poleR.toFixed(4)}</span>
                  </div>
                  <div className="fg-stat">
                    <span className="fg-stat__k">Pole Angle θ_p</span>
                    <span className="fg-stat__v">{((lab1Pt.poleTheta * 180) / Math.PI).toFixed(1)}°</span>
                  </div>
                  <div className="fg-stat">
                    <span className="fg-stat__k">Zero Radius |z_z|</span>
                    <span className="fg-stat__v">{lab1Pt.zeroR.toFixed(4)}</span>
                  </div>
                  <div className="fg-stat">
                    <span className="fg-stat__k">Stability Margin</span>
                    <span className="fg-stat__v" style={{ color: "oklch(0.78 0.15 156)" }}>
                      1 − |z| = {(1 - lab1Pt.poleR).toFixed(4)}
                    </span>
                  </div>
                </div>
              </div>

              <div className="fg-bench__controls">
                <SliderControl
                  id="l1-phz"
                  label="Pole Frequency (×)"
                  min={80}
                  max={8500}
                  step={10}
                  value={dipole.poleHz}
                  displayValue={`${dipole.poleHz} Hz`}
                  onChange={(v) => setDipole((d) => ({ ...d, poleHz: v }))}
                />
                <SliderControl
                  id="l1-pq"
                  label="Pole Resonance (Q)"
                  min={0.7}
                  max={36}
                  step={0.1}
                  value={dipole.poleQ}
                  displayValue={`Q ${dipole.poleQ.toFixed(1)}`}
                  onChange={(v) => setDipole((d) => ({ ...d, poleQ: v }))}
                />
                <SliderControl
                  id="l1-zhz"
                  label="Zero Notch Frequency (○)"
                  min={80}
                  max={10500}
                  step={10}
                  value={dipole.zeroHz}
                  displayValue={`${dipole.zeroHz} Hz`}
                  onChange={(v) => setDipole((d) => ({ ...d, zeroHz: v }))}
                />
                <SliderControl
                  id="l1-zq"
                  label="Zero Notch Sharpness"
                  min={0.7}
                  max={20}
                  step={0.1}
                  value={dipole.zeroQ}
                  displayValue={`Q ${dipole.zeroQ.toFixed(1)}`}
                  onChange={(v) => setDipole((d) => ({ ...d, zeroQ: v }))}
                />
              </div>
            </div>
          </div>

          {showEngineer && (
            <CodeExcerpt
              file="products/pole-field/engine/src/pole_zero.h"
              lines="27-92"
              code={CPP_DIPOLE}
            />
          )}
        </section>

        {/* ============================================================
            LAB 2: WHY NAIVE INTERPOLATION COLLAPSES / EXPLODES
           ============================================================ */}
        <section className="fg-chapter" id="polar-stability" aria-labelledby="ch2-title">
          <div className="fg-chapter__head">
            <span className="fg-chapter__num">02 · The E-MU Morpheus Secret</span>
            <h2 id="ch2-title">Why Naive Filter Morphs Lose Their Bite — and Log-Polar Wins</h2>
          </div>

          <div className="fg-layers" data-depth={depth}>
            {showMusician && (
              <article className="fg-layer fg-layer--musician">
                <span className="fg-layer__tag">Layer 1 · The Musical Ear</span>
                <p>
                  Ever notice how crossfading two ordinary EQ presets sounds lifeless in the middle,
                  or why modulating classic digital filters fast makes them click or blow up?
                </p>
                <p>
                  If you simply blend two filter formulas in a straight line, the resonant peaks
                  cut straight across the dull center of the circle — <strong>losing almost all their
                  resonance halfway through the sweep</strong>. Pole Field glides every pole along a
                  curved musical arc (logarithmic frequency and Q), so your formant stays razor-sharp
                  and musical at every single millimeter between corners.
                </p>
              </article>
            )}
            {showEngineer && (
              <article className="fg-layer fg-layer--engineer">
                <span className="fg-layer__tag">Layer 2 · Under the Hood (Log-Polar Trilinear Morph)</span>
                <p>
                  Interpolating direct-form biquad coefficients <code>(a₁, a₂)</code> or Cartesian
                  root coordinates <code>(x, y)</code> cuts a straight chord inside the unit circle:
                  the radius <code>|z|</code> collapses at <code>t = 0.5</code> (and rapid modulation
                  of direct-form delay states injects energy spikes that blow up past{" "}
                  <code>|z| ≥ 1</code>).
                </p>
                <p>
                  Pole Field stores all 8 corners of every 3D cube in continuous{" "}
                  <code>(poleHz, poleQ, zeroHz, zeroQ)</code>, trilinearly interpolates in{" "}
                  <strong>log-frequency and log-Q</strong> (<code>logTrilerp</code>), and smooths
                  each stage in log-polar space over 6 ms inside the TPT SVF.
                </p>
              </article>
            )}
          </div>

          <div className="fg-bench" aria-label="Polar versus Cartesian interpolation lab">
            <div className="fg-bench__bar">
              <span className="fg-bench__title">
                Lab 2 · Scrub t (220 Hz, Q=26 → 5,200 Hz, Q=26) · Emerald = Log-Polar · Red Dashed = Cartesian
              </span>
            </div>

            <div className="fg-bench__body fg-bench__body--wide">
              <div className="fg-bench__stage">
                <div className="fg-canvas-pair">
                  <div className="fg-canvas-box">
                    <span className="fg-canvas-label">Pole Trajectory Inside |z| = 1</span>
                    <canvas
                      ref={lab2ZRef}
                      width={320}
                      height={240}
                      aria-label="Comparison of curved Log-Polar arc versus straight Cartesian chord"
                    />
                  </div>
                  <div className="fg-canvas-box">
                    <span className="fg-canvas-label">Resulting Peak at Current Morph t</span>
                    <canvas
                      ref={lab2BodeRef}
                      width={420}
                      height={240}
                      aria-label="Bode plot comparing Log-Polar peak preservation against Cartesian Q collapse"
                    />
                  </div>
                </div>
              </div>

              <div className="fg-bench__controls">
                <SliderControl
                  id="l2-t"
                  label="Morph Position (t)"
                  min={0}
                  max={1}
                  step={0.01}
                  value={morphT}
                  displayValue={`${Math.round(morphT * 100)}%`}
                  onChange={setMorphT}
                />
                <div className="fg-readout">
                  <div className="fg-stat">
                    <span className="fg-stat__k">Log-Polar Q</span>
                    <span className="fg-stat__v" style={{ color: "oklch(0.78 0.15 156)" }}>
                      Q {lab2Data.polar.q.toFixed(1)}
                    </span>
                  </div>
                  <div className="fg-stat">
                    <span className="fg-stat__k">Cartesian Q</span>
                    <span className="fg-stat__v" style={{ color: "oklch(0.74 0.16 18)" }}>
                      Q {lab2Data.cartesian.q.toFixed(1)}
                    </span>
                  </div>
                  <div className="fg-stat">
                    <span className="fg-stat__k">Log-Polar Freq</span>
                    <span className="fg-stat__v">{Math.round(lab2Data.polar.hz)} Hz</span>
                  </div>
                  <div className="fg-stat">
                    <span className="fg-stat__k">Cartesian |z|</span>
                    <span className="fg-stat__v">{lab2Data.cartesian.r.toFixed(3)}</span>
                  </div>
                </div>
                <p style={{ fontSize: "0.78rem", color: "var(--screen-dim)", lineHeight: 1.48 }}>
                  At <code>t = 50%</code>, Cartesian interpolation drops resonance from{" "}
                  <strong>Q = 26</strong> all the way down to <strong>Q ≈ 2</strong> — flattening
                  the talking peak completely. Log-Polar stays locked at <strong>Q = 26</strong>{" "}
                  through the entire musical sweep!
                </p>
              </div>
            </div>
          </div>

          {showEngineer && (
            <CodeExcerpt
              file="products/pole-field/core/src/cubes.cpp"
              lines="1207-1244"
              code={CPP_POLAR}
            />
          )}
        </section>

        {/* ============================================================
            LAB 3: LIVE 14-POLE MORPH CUBE BENCH (REAL ENGINE IN BROWSER)
           ============================================================ */}
        <section className="fg-chapter" id="morph-cube" aria-labelledby="ch3-title">
          <div className="fg-chapter__head">
            <span className="fg-chapter__num">03 · The Full 14-Pole Engine Live in Your Browser</span>
            <h2 id="ch3-title">Seven Dipoles, Three Topologies, One Sub-Locked Cube</h2>
          </div>

          <div className="fg-layers" data-depth={depth}>
            {showMusician && (
              <article className="fg-layer fg-layer--musician">
                <span className="fg-layer__tag">Layer 1 · The Musical Ear</span>
                <p>
                  Stacking <strong>7 pole-zero dipoles</strong> (14 poles + 14 zeros) lets a single
                  XY gesture sculpt entire vocal tracts, razor phasers, and metallic ridges at once.
                  Click <strong>Play 14-Pole Lattice</strong> below and drag inside the Morph Pad —
                  then flip <strong>Sub Lock</strong> off and back on to hear how protecting the
                  mono fundamental below 105 Hz keeps neuro bass club-solid even under extreme
                  filtering.
                </p>
              </article>
            )}
            {showEngineer && (
              <article className="fg-layer fg-layer--engineer">
                <span className="fg-layer__tag">Layer 2 · Under the Hood (Alternating Dipoles &amp; LR4 Sub Lock)</span>
                <p>
                  Notice two subtle engineering decisions inside Pole Field’s real C++:
                  <br />
                  1. <strong>Alternating dipole orientation:</strong> even stages place the zero{" "}
                  <em>above</em> the pole; odd stages place it <em>below</em>, so cumulative shelf
                  tilts cancel out naturally across all 14 poles.
                  <br />
                  2. <strong>Exact 0.00 dB LR4 Sub Lock:</strong> pre-lattice highs are high-passed
                  so inter-stage drive never creates muddy sub-band intermodulation, while
                  post-lattice Mid sums cleanly with the mono LR4 sub to an exact flat all-pass!
                </p>
              </article>
            )}
          </div>

          <div className="fg-bench" aria-label="Live 14-pole morph cube bench">
            <div className="fg-bench__bar">
              <span className="fg-bench__title">
                Lab 3 · Live 7-Stage Cytomic TPT Lattice (AudioWorklet)
              </span>
              <div className="fg-chips">
                <button
                  type="button"
                  className="fg-chip fg-chip--play"
                  aria-pressed={activeAudioLab === "lab3"}
                  onClick={() => toggleLabAudio("lab3")}
                >
                  {activeAudioLab === "lab3" ? "■ Stop Lattice" : "▶ Play 14-Pole Lattice"}
                </button>
                {RECIPES.map((r) => (
                  <button
                    key={r.name}
                    type="button"
                    className="fg-chip"
                    onClick={() => {
                      setCubeIndex(r.cubeIndex);
                      setMorphX(r.morphX);
                      setMorphY(r.morphY);
                      setMorphZ(r.morphZ);
                      setRes(r.res);
                      setTopology(r.topology);
                      setStagesCount(r.stages);
                      setDrive(r.drive);
                      setDrivePos(r.drivePos);
                      setAudioSource(r.source);
                    }}
                  >
                    {r.name}
                  </button>
                ))}
              </div>
            </div>

            <div className="fg-bench__body fg-bench__body--split">
              <div className="fg-bench__stage">
                <div className="fg-canvas-box fg-canvas-box--interactive">
                  <span className="fg-canvas-label">
                    XY Morph Pad · {FACTORY_CUBES[cubeIndex].name} (drag to morph)
                  </span>
                  <canvas
                    ref={lab3PadRef}
                    width={520}
                    height={210}
                    aria-label="Interactive 2D XY morph pad for Pole Field 3D filter cube"
                    onPointerDown={(e) => handlePadPointer(e, true)}
                    onPointerMove={(e) => handlePadPointer(e, false)}
                    onPointerUp={() => {
                      padDraggingRef.current = false;
                    }}
                    onPointerCancel={() => {
                      padDraggingRef.current = false;
                    }}
                  />
                </div>

                <div className="fg-canvas-pair">
                  <div className="fg-canvas-box">
                    <span className="fg-canvas-label">{stagesCount * 2}-Pole Unit Circle</span>
                    <canvas
                      ref={lab3ZRef}
                      width={260}
                      height={220}
                      aria-label="14-pole Z-plane unit circle visualization"
                    />
                  </div>
                  <div className="fg-canvas-box">
                    <span className="fg-canvas-label">Live Lattice Magnitude</span>
                    <canvas
                      ref={lab3BodeRef}
                      width={360}
                      height={220}
                      aria-label="14-pole magnitude curve visualization"
                    />
                  </div>
                </div>
              </div>

              <div className="fg-bench__controls">
                <div>
                  <span className="fg-stat__k" style={{ display: "block", marginBlockEnd: "0.35rem" }}>
                    Cube Bank
                  </span>
                  <div className="fg-chips" role="group" aria-label="Filter cube">
                    {FACTORY_CUBES.map((c, idx) => (
                      <button
                        key={c.id}
                        type="button"
                        className="fg-chip"
                        aria-pressed={cubeIndex === idx}
                        onClick={() => setCubeIndex(idx)}
                      >
                        {c.name}
                      </button>
                    ))}
                  </div>
                </div>

                <div style={{ display: "grid", gap: "0.5rem", gridTemplateColumns: "repeat(auto-fit, minmax(11rem, 1fr))" }}>
                  <div>
                    <span className="fg-stat__k" style={{ display: "block", marginBlockEnd: "0.35rem" }}>
                      Topology
                    </span>
                    <div className="fg-chips" role="group" aria-label="Filter topology">
                      {["Serial", "Hybrid", "Parallel"].map((name, idx) => (
                        <button
                          key={name}
                          type="button"
                          className="fg-chip"
                          aria-pressed={topology === idx}
                          onClick={() => setTopology(idx)}
                        >
                          {name}
                        </button>
                      ))}
                    </div>
                  </div>
                  <div>
                    <span className="fg-stat__k" style={{ display: "block", marginBlockEnd: "0.35rem" }}>
                      Test Source
                    </span>
                    <div className="fg-chips" role="group" aria-label="Test audio source">
                      {[
                        ["reese", "Neuro Reese"],
                        ["chord", "Minor-9 Saw"],
                        ["drums", "172 Break"],
                      ].map(([id, label]) => (
                        <button
                          key={id}
                          type="button"
                          className="fg-chip"
                          aria-pressed={audioSource === id}
                          onClick={() => setAudioSource(id)}
                        >
                          {label}
                        </button>
                      ))}
                    </div>
                  </div>
                </div>

                <SliderControl
                  id="l3-z"
                  label="Morph Z (Front ↔ Back Face)"
                  min={0}
                  max={1}
                  step={0.01}
                  value={morphZ}
                  displayValue={`${Math.round(morphZ * 100)}%`}
                  onChange={setMorphZ}
                />
                <SliderControl
                  id="l3-res"
                  label="Resonance"
                  min={0}
                  max={1}
                  step={0.01}
                  value={res}
                  displayValue={`${Math.round(res * 100)}%`}
                  onChange={setRes}
                />
                <SliderControl
                  id="l3-drive"
                  label="Saturation Drive"
                  min={0}
                  max={1}
                  step={0.01}
                  value={drive}
                  displayValue={`${Math.round(drive * 100)}%`}
                  onChange={setDrive}
                />

                <div className="fg-chips">
                  {[
                    [2, "4P"],
                    [4, "8P"],
                    [6, "12P"],
                    [7, "14P"],
                  ].map(([n, lbl]) => (
                    <button
                      key={lbl}
                      type="button"
                      className="fg-chip"
                      aria-pressed={stagesCount === n}
                      onClick={() => setStagesCount(n)}
                    >
                      {lbl}
                    </button>
                  ))}
                  {["Pre", "Inter-Stage", "Post"].map((lbl, idx) => (
                    <button
                      key={lbl}
                      type="button"
                      className="fg-chip"
                      aria-pressed={drivePos === idx}
                      onClick={() => setDrivePos(idx)}
                    >
                      {lbl}
                    </button>
                  ))}
                  <button
                    type="button"
                    className="fg-chip"
                    aria-pressed={subLock}
                    onClick={() => setSubLock((v) => !v)}
                  >
                    Sub Lock: {subLock ? "ON" : "OFF"}
                  </button>
                </div>
              </div>
            </div>
          </div>

          {showEngineer && (
            <CodeExcerpt
              file="products/pole-field/engine/src/engine.cpp"
              lines="35-45"
              code={CPP_SUBLOCK}
            />
          )}
        </section>

        {/* ============================================================
            LAB 4: INTER-STAGE DRIVE & ADAA2 ANTI-ALIASING
           ============================================================ */}
        <section className="fg-chapter" id="adaa2" aria-labelledby="ch4-title">
          <div className="fg-chapter__head">
            <span className="fg-chapter__num">04 · Clean Analog Bite Without Digital Grime</span>
            <h2 id="ch4-title">Inter-Stage Saturation &amp; Why ADAA2 Kills Aliasing</h2>
          </div>

          <div className="fg-layers" data-depth={depth}>
            {showMusician && (
              <article className="fg-layer fg-layer--musician">
                <span className="fg-layer__tag">Layer 1 · The Musical Ear</span>
                <p>
                  Pushing hard distortion inside a plugin generates crisp upper harmonics — until
                  those harmonics bounce off the sample-rate ceiling (24 kHz) and fold backward as
                  ugly, dissonant metallic buzz (<strong>aliasing</strong>).
                </p>
                <p>
                  In Lab 4 below, sweep the frequency or turn up Drive and watch the{" "}
                  <strong style={{ color: "oklch(0.74 0.16 18)" }}>red naive spectrum</strong> fill
                  with inharmonic hash between the real overtones, while{" "}
                  <strong style={{ color: "oklch(0.76 0.15 156)" }}>ADAA2</strong> carves that hash
                  down by 15–30 dB <em>before</em> oversampling even kicks in.
                </p>
              </article>
            )}
            {showEngineer && (
              <article className="fg-layer fg-layer--engineer">
                <span className="fg-layer__tag">Layer 2 · Under the Hood (Continuous-Time Antiderivatives)</span>
                <p>
                  Instead of evaluating a waveshaper <code>f(x[n])</code> at discrete instants,
                  second-order Antiderivative Anti-Aliasing (Parker/Bilbao et al., DAFx-16/17)
                  analytically integrates the waveshaper twice over a triangular kernel using its
                  closed-form antiderivatives <code>F₁</code> and <code>F₂</code>, paired with HIIR
                  2×/4×/8× polyphase oversampling in <code>modules/dsp/include/hba/dsp/adaa.h</code>.
                </p>
                <div className="fg-formula">
                  ADAA1: y[n] = (F₁(x[n]) − F₁(x[n−1])) / (x[n] − x[n−1])
                </div>
              </article>
            )}
          </div>

          <div className="fg-bench" aria-label="ADAA2 aliasing comparison lab">
            <div className="fg-bench__bar">
              <span className="fg-bench__title">
                Lab 4 · Red = Naive Waveshaper · Emerald = ADAA Anti-Aliased (48 kHz)
              </span>
              <div className="fg-chips">
                <button
                  type="button"
                  className="fg-chip fg-chip--play"
                  aria-pressed={activeAudioLab === "lab4"}
                  onClick={() => toggleLabAudio("lab4")}
                >
                  {activeAudioLab === "lab4" ? "■ Stop Tone" : "▶ Hear Aliasing A/B"}
                </button>
                <button
                  type="button"
                  className="fg-chip"
                  aria-pressed={adaaEnabled}
                  onClick={() => setAdaaEnabled((v) => !v)}
                >
                  Audio Mode: {adaaEnabled ? "ADAA ON (Clean)" : "NAIVE (Aliased)"}
                </button>
              </div>
            </div>

            <div className="fg-bench__body fg-bench__body--wide">
              <div className="fg-bench__stage">
                <div className="fg-canvas-box">
                  <span className="fg-canvas-label">FFT Spectrum (0 – 24 kHz, −96 to 0 dB)</span>
                  <canvas
                    ref={lab4SpecRef}
                    width={640}
                    height={250}
                    aria-label="FFT spectrum comparing Naive and ADAA2 aliasing peaks"
                  />
                </div>
                <div className="fg-readout">
                  <div className="fg-stat">
                    <span className="fg-stat__k">Worst Naive Alias</span>
                    <span className="fg-stat__v" style={{ color: "oklch(0.74 0.16 18)" }}>
                      {lab4Data.naiveAliasDb.toFixed(1)} dB
                    </span>
                  </div>
                  <div className="fg-stat">
                    <span className="fg-stat__k">Worst ADAA Alias</span>
                    <span className="fg-stat__v" style={{ color: "oklch(0.78 0.15 156)" }}>
                      {lab4Data.adaaAliasDb.toFixed(1)} dB
                    </span>
                  </div>
                  <div className="fg-stat">
                    <span className="fg-stat__k">Alias Reduction</span>
                    <span className="fg-stat__v" style={{ color: "oklch(0.80 0.14 202)" }}>
                      {lab4Data.reductionDb >= 0 ? `-${lab4Data.reductionDb.toFixed(1)} dB` : "0 dB"}
                    </span>
                  </div>
                </div>
              </div>

              <div className="fg-bench__controls">
                <div>
                  <span className="fg-stat__k" style={{ display: "block", marginBlockEnd: "0.35rem" }}>
                    Waveshaper Curve
                  </span>
                  <div className="fg-chips">
                    {[
                      ["buchla", "Buchla 259 Fold"],
                      ["clip", "Hard Clip"],
                      ["tanh", "Tanh Tube"],
                    ].map(([id, label]) => (
                      <button
                        key={id}
                        type="button"
                        className="fg-chip"
                        aria-pressed={adaaShaper === id}
                        onClick={() => setAdaaShaper(id)}
                      >
                        {label}
                      </button>
                    ))}
                  </div>
                </div>
                <div>
                  <span className="fg-stat__k" style={{ display: "block", marginBlockEnd: "0.35rem" }}>
                    ADAA2 Oversampling Rate
                  </span>
                  <div className="fg-chips">
                    {[
                      [1, "1× ADAA2 Only"],
                      [2, "2× OS + ADAA2 (Pole Field)"],
                    ].map(([rate, label]) => (
                      <button
                        key={rate}
                        type="button"
                        className="fg-chip"
                        aria-pressed={adaaOs === rate}
                        onClick={() => setAdaaOs(rate)}
                      >
                        {label}
                      </button>
                    ))}
                  </div>
                </div>
                <SliderControl
                  id="l4-drive"
                  label="Input Drive"
                  min={1.2}
                  max={10}
                  step={0.1}
                  value={adaaDrive}
                  displayValue={`${adaaDrive.toFixed(1)}×`}
                  onChange={setAdaaDrive}
                />
                <SliderControl
                  id="l4-freq"
                  label="Test Sine Frequency"
                  min={420}
                  max={5400}
                  step={10}
                  value={adaaFreq}
                  displayValue={`${adaaFreq} Hz`}
                  onChange={setAdaaFreq}
                />
              </div>
            </div>
          </div>

          {showEngineer && (
            <CodeExcerpt
              file="modules/dsp/include/hba/dsp/adaa.h"
              lines="101-127"
              code={CPP_ADAA}
            />
          )}
        </section>

        {/* ============================================================
            LAB 5: LIVE 14TH-ORDER BURG LPC AUDIO IMPRINTING
           ============================================================ */}
        <section className="fg-chapter" id="burg-lpc" aria-labelledby="ch5-title">
          <div className="fg-chapter__head">
            <span className="fg-chapter__num">05 · Steal the Body of Any Sound</span>
            <h2 id="ch5-title">14th-Order Burg LPC Imprinting — Live in Your Browser</h2>
          </div>

          <div className="fg-layers" data-depth={depth}>
            {showMusician && (
              <article className="fg-layer fg-layer--musician">
                <span className="fg-layer__tag">Layer 1 · The Musical Ear</span>
                <p>
                  Every acoustic sound — a shouted vowel, a metallic snare rim, a tuba blast, a
                  struck wine glass — has a hidden fingerprint of resonant peaks. Pole Field lets
                  you drop <em>any WAV sample</em> onto a cube corner and stamp its exact 14-pole
                  resonant body onto your synths or drums.
                </p>
                <p>
                  Try the built-in targets below or drop any audio file from your drive (processed
                  100% locally in your browser — nothing is uploaded), then hit{" "}
                  <strong>Audition Imprinted Lattice</strong> to play a Neuro Reese right through
                  its extracted formants.
                </p>
              </article>
            )}
            {showEngineer && (
              <article className="fg-layer fg-layer--engineer">
                <span className="fg-layer__tag">Layer 2 · Under the Hood (Burg’s Method + Durand-Kerner)</span>
                <p>
                  Pole Field picks the highest sustained tonal frame (penalizing brief transient
                  clicks via crest factor), decimates cleanly to ~24 kHz, fits a{" "}
                  <strong>14th-order all-pole AR model via Burg’s lattice recursion</strong> (whose
                  reflection coefficients <code>|k_m| &lt; 1</code> guarantee minimum-phase
                  stability!), and solves all 14 complex polynomial roots simultaneously with{" "}
                  <strong>Durand-Kerner iteration</strong> before placing alternating dipole zeros.
                </p>
              </article>
            )}
          </div>

          <div className="fg-bench" aria-label="Live 14th-order Burg LPC audio imprinting lab">
            <div className="fg-bench__bar">
              <span className="fg-bench__title">Lab 5 · {imprintLabel}</span>
              <div className="fg-chips">
                <button
                  type="button"
                  className="fg-chip fg-chip--play"
                  aria-pressed={activeAudioLab === "lab5"}
                  onClick={() => toggleLabAudio("lab5")}
                >
                  {activeAudioLab === "lab5" ? "■ Stop Imprint" : "▶ Audition Imprinted Lattice"}
                </button>
              </div>
            </div>

            <div className="fg-bench__body fg-bench__body--wide">
              <div className="fg-bench__stage">
                <div className="fg-canvas-pair">
                  <div className="fg-canvas-box">
                    <span className="fg-canvas-label">Extracted 14-Pole Roots</span>
                    <canvas
                      ref={lab5ZRef}
                      width={300}
                      height={240}
                      aria-label="Z-plane roots extracted by 14th-order Burg LPC"
                    />
                  </div>
                  <div className="fg-canvas-box">
                    <span className="fg-canvas-label">Imprinted Filter Response</span>
                    <canvas
                      ref={lab5BodeRef}
                      width={420}
                      height={240}
                      aria-label="Frequency response of imprinted filter"
                    />
                  </div>
                </div>
              </div>

              <div className="fg-bench__controls">
                <div>
                  <span className="fg-stat__k" style={{ display: "block", marginBlockEnd: "0.35rem" }}>
                    Built-In Targets (Runs Real Burg LPC Live)
                  </span>
                  <div className="fg-chips">
                    {[
                      ["vowel", "Human 'Ah' Vowel"],
                      ["snare", "Metallic Snare"],
                      ["brass", "Brass Growl"],
                      ["bell", "Glass Bell"],
                    ].map(([id, label]) => (
                      <button
                        key={id}
                        type="button"
                        className="fg-chip"
                        aria-pressed={imprintKind === id}
                        onClick={() => handleSelectSyntheticImprint(id, `${label} (built-in)`)}
                      >
                        {label}
                      </button>
                    ))}
                  </div>
                </div>

                <label className="fg-dropzone">
                  <span>Or imprint your own audio file (100% local, zero upload):</span>
                  <input type="file" accept="audio/*" onChange={handleAudioUpload} />
                </label>

                <div className="fg-readout">
                  {imprintCorner.stages.slice(0, 4).map((st, idx) => (
                    <div key={idx} className="fg-stat">
                      <span className="fg-stat__k">Formant F{idx + 1}</span>
                      <span className="fg-stat__v">
                        {Math.round(st.poleHz)} Hz · Q{st.poleQ.toFixed(1)}
                      </span>
                    </div>
                  ))}
                </div>
              </div>
            </div>
          </div>

          {showEngineer && (
            <CodeExcerpt
              file="products/pole-field/core/src/cubes.cpp"
              lines="1428-1485"
              code={CPP_BURG}
            />
          )}
        </section>

        {/* ============================================================
            CLOSING COPYLEFT & STUDIO DOWNLOAD CTA
           ============================================================ */}
        <section className="fg-chapter" style={{ borderBlockEnd: 0 }}>
          <div className="fg-cta">
            <p className="fg-eyebrow" style={{ color: "oklch(0.82 0.14 318)" }}>
              Open Copyleft Craft · Built by Sean Dearnaley
            </p>
            <h2 style={{ fontSize: "var(--step-2)" }}>
              Read the Code, Compile It Yourself — or Grab the Signed Studio Build
            </h2>
            <div className="fg-cta__grid">
              <div style={{ display: "grid", gap: "0.75rem", alignContent: "start" }}>
                <p>
                  Everything you just played with runs straight inside{" "}
                  <strong>Pole Field</strong> as a zero-allocation{" "}
                  <code>[[clang::nonblocking]]</code> C++20 VST3, AU, and Standalone instrument with
                  a 60fps Visage GPU interface, 12 morph cubes, 42 presets, audio-rate FM, and 4
                  drawable modulation lanes.
                </p>
                <div className="fg-chips">
                  <a className="fg-cta__btn" href="/#pole-field">
                    Download Signed macOS / Win / Linux Build
                  </a>
                  <a
                    className="fg-cta__btn fg-cta__btn--ghost"
                    href={`${REPO}/tree/main/products/pole-field`}
                  >
                    Browse Pole Field C++ on GitHub ↗
                  </a>
                </div>
              </div>
              <div className="fg-code-card">
                <div className="fg-code-card__bar">
                  <span>Compile from source (AGPL-3.0-or-later)</span>
                  <Link href="/learn">All Field Guides →</Link>
                </div>
                <pre>
                  <code>{`git clone ${REPO}.git
cd human-being-audio/products/pole-field
cmake --preset dev
cmake --build --preset dev && ctest --preset dev`}</code>
                </pre>
              </div>
            </div>
          </div>
        </section>
      </div>
    </main>
  );
}
