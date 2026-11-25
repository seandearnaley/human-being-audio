// Exact JavaScript mirror of Pole Field's shipping C++20 DSP core:
//   - products/pole-field/core/src/cubes.cpp  (cubes, resolveLattice, evaluateMagnitudeDb,
//                                              stageZPoint, 14th-order Burg LPC + Durand-Kerner)
//   - products/pole-field/engine/src/pole_zero.h (7-stage Cytomic TPT SVF + passive OTA damping)
//   - modules/dsp/include/hba/dsp/adaa.h      (ADAA2 anti-aliased waveshaping)

export const SAMPLE_RATE = 48000;
export const NUM_STAGES = 7;

// Exact 8-corner factory cube definitions copied verbatim from products/pole-field/core/src/cubes.cpp
export const FACTORY_CUBES = [
  {
    id: "neuro-chew",
    name: "Neuro Chew",
    description: "Iconic E-MU Morpheus talking neuro bass cube",
    cornerNames: [
      "Vocal chew",
      "Phaser comb",
      "Hollow acid",
      "Metal ridge",
      "u-Throat",
      "Triple notch",
      "Razor formant",
      "Acid scream",
    ],
    corners: [
      {
        makeupDb: 0.0,
        stages: [
          { poleHz: 290, poleQ: 4.2, zeroHz: 410, zeroQ: 3.6 },
          { poleHz: 660, poleQ: 5.8, zeroHz: 520, zeroQ: 4.4 },
          { poleHz: 1420, poleQ: 6.2, zeroHz: 1820, zeroQ: 5.0 },
          { poleHz: 2380, poleQ: 6.6, zeroHz: 1980, zeroQ: 5.2 },
          { poleHz: 3450, poleQ: 5.6, zeroHz: 4350, zeroQ: 4.6 },
          { poleHz: 5600, poleQ: 4.4, zeroHz: 4700, zeroQ: 3.8 },
          { poleHz: 9400, poleQ: 3.2, zeroHz: 11800, zeroQ: 2.8 },
        ],
      },
      {
        makeupDb: 0.5,
        stages: [
          { poleHz: 185, poleQ: 4.6, zeroHz: 235, zeroQ: 7.2 },
          { poleHz: 450, poleQ: 5.2, zeroHz: 355, zeroQ: 7.6 },
          { poleHz: 920, poleQ: 5.8, zeroHz: 1180, zeroQ: 8.0 },
          { poleHz: 1860, poleQ: 6.2, zeroHz: 1480, zeroQ: 7.8 },
          { poleHz: 3520, poleQ: 5.6, zeroHz: 4480, zeroQ: 7.2 },
          { poleHz: 6450, poleQ: 4.6, zeroHz: 5200, zeroQ: 6.2 },
          { poleHz: 11200, poleQ: 3.4, zeroHz: 13600, zeroQ: 4.8 },
        ],
      },
      {
        makeupDb: 0.0,
        stages: [
          { poleHz: 170, poleQ: 5.4, zeroHz: 285, zeroQ: 4.4 },
          { poleHz: 510, poleQ: 5.0, zeroHz: 395, zeroQ: 6.2 },
          { poleHz: 1040, poleQ: 3.8, zeroHz: 1380, zeroQ: 7.2 },
          { poleHz: 1820, poleQ: 7.2, zeroHz: 1480, zeroQ: 5.0 },
          { poleHz: 3120, poleQ: 5.4, zeroHz: 3980, zeroQ: 5.8 },
          { poleHz: 5450, poleQ: 4.2, zeroHz: 4450, zeroQ: 4.4 },
          { poleHz: 9600, poleQ: 3.2, zeroHz: 12100, zeroQ: 3.4 },
        ],
      },
      {
        makeupDb: -0.5,
        stages: [
          { poleHz: 250, poleQ: 4.4, zeroHz: 310, zeroQ: 4.8 },
          { poleHz: 640, poleQ: 6.8, zeroHz: 520, zeroQ: 5.6 },
          { poleHz: 1490, poleQ: 7.8, zeroHz: 1790, zeroQ: 6.4 },
          { poleHz: 2680, poleQ: 7.6, zeroHz: 2240, zeroQ: 6.2 },
          { poleHz: 4350, poleQ: 7.0, zeroHz: 5120, zeroQ: 5.8 },
          { poleHz: 6900, poleQ: 5.8, zeroHz: 5850, zeroQ: 5.0 },
          { poleHz: 10800, poleQ: 4.4, zeroHz: 12800, zeroQ: 4.0 },
        ],
      },
      {
        makeupDb: 0.0,
        stages: [
          { poleHz: 220, poleQ: 5.8, zeroHz: 360, zeroQ: 4.8 },
          { poleHz: 580, poleQ: 6.2, zeroHz: 450, zeroQ: 5.6 },
          { poleHz: 1220, poleQ: 3.6, zeroHz: 1620, zeroQ: 6.6 },
          { poleHz: 2320, poleQ: 6.0, zeroHz: 1920, zeroQ: 5.0 },
          { poleHz: 3250, poleQ: 5.4, zeroHz: 4180, zeroQ: 5.0 },
          { poleHz: 5300, poleQ: 3.8, zeroHz: 4350, zeroQ: 4.2 },
          { poleHz: 8800, poleQ: 2.8, zeroHz: 11400, zeroQ: 3.0 },
        ],
      },
      {
        makeupDb: 0.5,
        stages: [
          { poleHz: 210, poleQ: 4.0, zeroHz: 305, zeroQ: 8.4 },
          { poleHz: 540, poleQ: 4.8, zeroHz: 420, zeroQ: 8.8 },
          { poleHz: 1140, poleQ: 6.0, zeroHz: 1480, zeroQ: 9.2 },
          { poleHz: 2150, poleQ: 6.6, zeroHz: 1720, zeroQ: 8.6 },
          { poleHz: 3920, poleQ: 5.4, zeroHz: 4980, zeroQ: 7.4 },
          { poleHz: 6850, poleQ: 4.4, zeroHz: 5600, zeroQ: 6.2 },
          { poleHz: 11400, poleQ: 3.2, zeroHz: 13800, zeroQ: 4.6 },
        ],
      },
      {
        makeupDb: -0.5,
        stages: [
          { poleHz: 340, poleQ: 6.4, zeroHz: 480, zeroQ: 5.6 },
          { poleHz: 860, poleQ: 7.4, zeroHz: 680, zeroQ: 6.4 },
          { poleHz: 1920, poleQ: 8.0, zeroHz: 2360, zeroQ: 6.6 },
          { poleHz: 2940, poleQ: 7.4, zeroHz: 2450, zeroQ: 6.0 },
          { poleHz: 4250, poleQ: 6.2, zeroHz: 5200, zeroQ: 5.2 },
          { poleHz: 6650, poleQ: 5.0, zeroHz: 5550, zeroQ: 4.6 },
          { poleHz: 10200, poleQ: 3.8, zeroHz: 12400, zeroQ: 3.6 },
        ],
      },
      {
        makeupDb: -0.5,
        stages: [
          { poleHz: 280, poleQ: 6.0, zeroHz: 205, zeroQ: 4.6 },
          { poleHz: 760, poleQ: 7.0, zeroHz: 940, zeroQ: 5.4 },
          { poleHz: 1640, poleQ: 7.8, zeroHz: 1320, zeroQ: 6.2 },
          { poleHz: 3080, poleQ: 7.4, zeroHz: 3780, zeroQ: 6.0 },
          { poleHz: 5100, poleQ: 6.2, zeroHz: 4200, zeroQ: 5.2 },
          { poleHz: 7900, poleQ: 5.0, zeroHz: 9500, zeroQ: 4.4 },
          { poleHz: 11800, poleQ: 3.8, zeroHz: 9800, zeroQ: 3.6 },
        ],
      },
    ],
  },
  {
    id: "vocal-matrix",
    name: "Vocal Matrix",
    description: "Hyper-articulate 8-vowel / anti-formant cube",
    cornerNames: ["Ah /a/", "Ee /i/", "Oh /o/", "Oo /u/", "Ae /æ/", "Eh /e/", "Nasal /n/", "Whisper"],
    corners: [
      {
        makeupDb: 0.0,
        stages: [
          { poleHz: 620, poleQ: 6.2, zeroHz: 820, zeroQ: 4.4 },
          { poleHz: 1080, poleQ: 6.8, zeroHz: 860, zeroQ: 4.8 },
          { poleHz: 2280, poleQ: 6.6, zeroHz: 2760, zeroQ: 4.6 },
          { poleHz: 2680, poleQ: 6.0, zeroHz: 2180, zeroQ: 4.4 },
          { poleHz: 3450, poleQ: 5.2, zeroHz: 4300, zeroQ: 4.0 },
          { poleHz: 5600, poleQ: 3.6, zeroHz: 4650, zeroQ: 3.2 },
          { poleHz: 9200, poleQ: 2.6, zeroHz: 11500, zeroQ: 2.4 },
        ],
      },
      {
        makeupDb: 0.0,
        stages: [
          { poleHz: 270, poleQ: 5.8, zeroHz: 420, zeroQ: 5.4 },
          { poleHz: 920, poleQ: 3.2, zeroHz: 680, zeroQ: 6.0 },
          { poleHz: 2150, poleQ: 7.2, zeroHz: 2680, zeroQ: 5.0 },
          { poleHz: 2950, poleQ: 6.8, zeroHz: 2420, zeroQ: 4.8 },
          { poleHz: 3680, poleQ: 6.0, zeroHz: 4550, zeroQ: 4.4 },
          { poleHz: 5900, poleQ: 3.8, zeroHz: 4900, zeroQ: 3.2 },
          { poleHz: 9600, poleQ: 2.8, zeroHz: 11800, zeroQ: 2.4 },
        ],
      },
      {
        makeupDb: 0.0,
        stages: [
          { poleHz: 420, poleQ: 6.2, zeroHz: 580, zeroQ: 4.8 },
          { poleHz: 780, poleQ: 6.8, zeroHz: 610, zeroQ: 5.4 },
          { poleHz: 1620, poleQ: 3.2, zeroHz: 2120, zeroQ: 5.8 },
          { poleHz: 2460, poleQ: 6.0, zeroHz: 1980, zeroQ: 4.8 },
          { poleHz: 3200, poleQ: 5.0, zeroHz: 4050, zeroQ: 4.2 },
          { poleHz: 5400, poleQ: 3.2, zeroHz: 4450, zeroQ: 3.0 },
          { poleHz: 8800, poleQ: 2.4, zeroHz: 11000, zeroQ: 2.2 },
        ],
      },
      {
        makeupDb: 0.0,
        stages: [
          { poleHz: 310, poleQ: 6.5, zeroHz: 440, zeroQ: 5.0 },
          { poleHz: 640, poleQ: 6.8, zeroHz: 490, zeroQ: 5.8 },
          { poleHz: 1380, poleQ: 2.8, zeroHz: 1860, zeroQ: 6.4 },
          { poleHz: 2380, poleQ: 5.6, zeroHz: 1920, zeroQ: 4.8 },
          { poleHz: 3150, poleQ: 4.6, zeroHz: 3950, zeroQ: 4.2 },
          { poleHz: 5200, poleQ: 3.0, zeroHz: 4300, zeroQ: 2.8 },
          { poleHz: 8400, poleQ: 2.2, zeroHz: 10600, zeroQ: 2.2 },
        ],
      },
      {
        makeupDb: -0.5,
        stages: [
          { poleHz: 660, poleQ: 6.6, zeroHz: 890, zeroQ: 4.8 },
          { poleHz: 1720, poleQ: 7.0, zeroHz: 1340, zeroQ: 5.2 },
          { poleHz: 2420, poleQ: 6.6, zeroHz: 2960, zeroQ: 4.8 },
          { poleHz: 3350, poleQ: 5.8, zeroHz: 2740, zeroQ: 4.4 },
          { poleHz: 4250, poleQ: 5.0, zeroHz: 5200, zeroQ: 4.0 },
          { poleHz: 6400, poleQ: 3.6, zeroHz: 5350, zeroQ: 3.2 },
          { poleHz: 9800, poleQ: 2.6, zeroHz: 12100, zeroQ: 2.4 },
        ],
      },
      {
        makeupDb: 0.0,
        stages: [
          { poleHz: 440, poleQ: 6.0, zeroHz: 620, zeroQ: 4.8 },
          { poleHz: 1640, poleQ: 7.0, zeroHz: 1260, zeroQ: 5.2 },
          { poleHz: 2480, poleQ: 6.6, zeroHz: 3050, zeroQ: 4.8 },
          { poleHz: 3180, poleQ: 5.8, zeroHz: 2600, zeroQ: 4.6 },
          { poleHz: 4100, poleQ: 4.8, zeroHz: 5050, zeroQ: 4.0 },
          { poleHz: 6200, poleQ: 3.4, zeroHz: 5150, zeroQ: 3.0 },
          { poleHz: 9400, poleQ: 2.4, zeroHz: 11600, zeroQ: 2.2 },
        ],
      },
      {
        makeupDb: 0.0,
        stages: [
          { poleHz: 280, poleQ: 5.4, zeroHz: 440, zeroQ: 7.8 },
          { poleHz: 1140, poleQ: 7.4, zeroHz: 860, zeroQ: 7.4 },
          { poleHz: 2120, poleQ: 6.4, zeroHz: 2650, zeroQ: 7.0 },
          { poleHz: 3050, poleQ: 5.8, zeroHz: 2480, zeroQ: 6.2 },
          { poleHz: 4300, poleQ: 4.8, zeroHz: 5300, zeroQ: 5.2 },
          { poleHz: 6600, poleQ: 3.6, zeroHz: 5450, zeroQ: 4.0 },
          { poleHz: 10000, poleQ: 2.8, zeroHz: 12300, zeroQ: 3.0 },
        ],
      },
      {
        makeupDb: 0.0,
        stages: [
          { poleHz: 420, poleQ: 3.2, zeroHz: 310, zeroQ: 5.4 },
          { poleHz: 1280, poleQ: 5.4, zeroHz: 1640, zeroQ: 6.0 },
          { poleHz: 2350, poleQ: 6.8, zeroHz: 1880, zeroQ: 5.6 },
          { poleHz: 3600, poleQ: 6.6, zeroHz: 4450, zeroQ: 5.4 },
          { poleHz: 5200, poleQ: 5.8, zeroHz: 4250, zeroQ: 4.8 },
          { poleHz: 7400, poleQ: 4.6, zeroHz: 9100, zeroQ: 4.0 },
          { poleHz: 11000, poleQ: 3.6, zeroHz: 9100, zeroQ: 3.4 },
        ],
      },
    ],
  },
  {
    id: "phase-razor",
    name: "Phase Razor",
    description: "Ultra-deep alternating notches & liquid comb sweeps",
    cornerNames: [
      "Deep comb",
      "Inverse rip",
      "Dual scoop",
      "Tube cavity",
      "Wide phase",
      "Jet notch",
      "Hollow sweep",
      "Steel pipe",
    ],
    corners: [
      {
        makeupDb: 0.5,
        stages: [
          { poleHz: 160, poleQ: 4.6, zeroHz: 205, zeroQ: 9.5 },
          { poleHz: 360, poleQ: 4.8, zeroHz: 285, zeroQ: 9.5 },
          { poleHz: 740, poleQ: 5.2, zeroHz: 930, zeroQ: 9.5 },
          { poleHz: 1520, poleQ: 5.6, zeroHz: 1210, zeroQ: 9.5 },
          { poleHz: 3040, poleQ: 5.4, zeroHz: 3820, zeroQ: 9.0 },
          { poleHz: 5900, poleQ: 4.6, zeroHz: 4750, zeroQ: 8.2 },
          { poleHz: 10800, poleQ: 3.6, zeroHz: 13200, zeroQ: 6.8 },
        ],
      },
      {
        makeupDb: 0.5,
        stages: [
          { poleHz: 220, poleQ: 5.2, zeroHz: 175, zeroQ: 9.2 },
          { poleHz: 490, poleQ: 5.6, zeroHz: 615, zeroQ: 9.4 },
          { poleHz: 1080, poleQ: 6.0, zeroHz: 860, zeroQ: 9.4 },
          { poleHz: 2260, poleQ: 6.2, zeroHz: 2840, zeroQ: 9.2 },
          { poleHz: 4520, poleQ: 5.6, zeroHz: 3640, zeroQ: 8.6 },
          { poleHz: 8100, poleQ: 4.4, zeroHz: 10100, zeroQ: 7.6 },
          { poleHz: 12600, poleQ: 3.4, zeroHz: 10200, zeroQ: 6.4 },
        ],
      },
      {
        makeupDb: 0.5,
        stages: [
          { poleHz: 250, poleQ: 5.8, zeroHz: 365, zeroQ: 9.8 },
          { poleHz: 540, poleQ: 3.6, zeroHz: 395, zeroQ: 9.8 },
          { poleHz: 960, poleQ: 5.8, zeroHz: 1380, zeroQ: 9.8 },
          { poleHz: 1920, poleQ: 3.8, zeroHz: 1460, zeroQ: 9.6 },
          { poleHz: 3550, poleQ: 6.0, zeroHz: 4750, zeroQ: 8.8 },
          { poleHz: 6600, poleQ: 4.2, zeroHz: 5150, zeroQ: 7.2 },
          { poleHz: 11200, poleQ: 3.0, zeroHz: 13800, zeroQ: 5.4 },
        ],
      },
      {
        makeupDb: 0.0,
        stages: [
          { poleHz: 195, poleQ: 6.6, zeroHz: 290, zeroQ: 6.4 },
          { poleHz: 585, poleQ: 6.6, zeroHz: 415, zeroQ: 6.4 },
          { poleHz: 975, poleQ: 6.4, zeroHz: 1280, zeroQ: 6.4 },
          { poleHz: 1560, poleQ: 6.2, zeroHz: 1210, zeroQ: 6.4 },
          { poleHz: 2540, poleQ: 5.8, zeroHz: 3250, zeroQ: 6.0 },
          { poleHz: 4480, poleQ: 5.0, zeroHz: 3550, zeroQ: 5.2 },
          { poleHz: 7800, poleQ: 4.0, zeroHz: 9800, zeroQ: 4.2 },
        ],
      },
      {
        makeupDb: 0.5,
        stages: [
          { poleHz: 140, poleQ: 5.4, zeroHz: 200, zeroQ: 9.6 },
          { poleHz: 390, poleQ: 5.8, zeroHz: 285, zeroQ: 9.6 },
          { poleHz: 940, poleQ: 6.2, zeroHz: 1310, zeroQ: 9.6 },
          { poleHz: 2180, poleQ: 6.4, zeroHz: 1620, zeroQ: 9.2 },
          { poleHz: 4600, poleQ: 5.8, zeroHz: 6200, zeroQ: 8.4 },
          { poleHz: 8400, poleQ: 4.6, zeroHz: 6450, zeroQ: 7.0 },
          { poleHz: 13000, poleQ: 3.2, zeroHz: 15200, zeroQ: 5.4 },
        ],
      },
      {
        makeupDb: 0.5,
        stages: [
          { poleHz: 310, poleQ: 6.2, zeroHz: 235, zeroQ: 9.6 },
          { poleHz: 780, poleQ: 6.6, zeroHz: 1020, zeroQ: 9.6 },
          { poleHz: 1840, poleQ: 6.8, zeroHz: 1400, zeroQ: 9.6 },
          { poleHz: 3900, poleQ: 6.4, zeroHz: 4980, zeroQ: 9.0 },
          { poleHz: 7200, poleQ: 5.6, zeroHz: 5750, zeroQ: 8.0 },
          { poleHz: 10800, poleQ: 4.2, zeroHz: 13200, zeroQ: 6.6 },
          { poleHz: 14400, poleQ: 3.0, zeroHz: 11800, zeroQ: 5.2 },
        ],
      },
      {
        makeupDb: 0.5,
        stages: [
          { poleHz: 205, poleQ: 5.0, zeroHz: 295, zeroQ: 9.6 },
          { poleHz: 540, poleQ: 5.4, zeroHz: 410, zeroQ: 9.6 },
          { poleHz: 1260, poleQ: 5.8, zeroHz: 1680, zeroQ: 9.4 },
          { poleHz: 2800, poleQ: 6.0, zeroHz: 2120, zeroQ: 9.0 },
          { poleHz: 5400, poleQ: 5.4, zeroHz: 7100, zeroQ: 8.2 },
          { poleHz: 9400, poleQ: 4.4, zeroHz: 7400, zeroQ: 7.0 },
          { poleHz: 13400, poleQ: 3.0, zeroHz: 15600, zeroQ: 5.4 },
        ],
      },
      {
        makeupDb: 0.0,
        stages: [
          { poleHz: 260, poleQ: 7.2, zeroHz: 350, zeroQ: 6.2 },
          { poleHz: 720, poleQ: 7.4, zeroHz: 560, zeroQ: 6.2 },
          { poleHz: 1480, poleQ: 7.2, zeroHz: 1920, zeroQ: 6.2 },
          { poleHz: 2920, poleQ: 6.8, zeroHz: 2280, zeroQ: 6.0 },
          { poleHz: 5200, poleQ: 6.0, zeroHz: 6600, zeroQ: 5.4 },
          { poleHz: 8600, poleQ: 4.8, zeroHz: 6900, zeroQ: 4.8 },
          { poleHz: 12400, poleQ: 3.8, zeroHz: 14800, zeroQ: 4.0 },
        ],
      },
    ],
  },
];

const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));

function linTrilerp(c000, c100, c010, c110, c001, c101, c011, c111, x, y, z) {
  const c00 = c000 + (c100 - c000) * x;
  const c10 = c010 + (c110 - c010) * x;
  const c01 = c001 + (c101 - c001) * x;
  const c11 = c011 + (c111 - c011) * x;
  const c0 = c00 + (c10 - c00) * y;
  const c1 = c01 + (c11 - c01) * y;
  return c0 + (c1 - c0) * z;
}

function logTrilerp(c000, c100, c010, c110, c001, c101, c011, c111, x, y, z) {
  return Math.exp(
    linTrilerp(
      Math.log(Math.max(1e-4, c000)),
      Math.log(Math.max(1e-4, c100)),
      Math.log(Math.max(1e-4, c010)),
      Math.log(Math.max(1e-4, c110)),
      Math.log(Math.max(1e-4, c001)),
      Math.log(Math.max(1e-4, c101)),
      Math.log(Math.max(1e-4, c011)),
      Math.log(Math.max(1e-4, c111)),
      x,
      y,
      z
    )
  );
}

// Complex arithmetic helpers
const cAdd = (a, b) => [a[0] + b[0], a[1] + b[1]];
const cSub = (a, b) => [a[0] - b[0], a[1] - b[1]];
const cMul = (a, b) => [a[0] * b[0] - a[1] * b[1], a[0] * b[1] + a[1] * b[0]];
const cDiv = (a, b) => {
  const d = b[0] * b[0] + b[1] * b[1] + 1e-30;
  return [(a[0] * b[0] + a[1] * b[1]) / d, (a[1] * b[0] - a[0] * b[1]) / d];
};
const cAbs = (a) => Math.hypot(a[0], a[1]);
const cArg = (a) => Math.atan2(a[1], a[0]);

export function stageZPoint(stage, sampleRate = SAMPLE_RATE) {
  const fs = Math.max(sampleRate, 22050);
  const bilinearRoot = (hz, q) => {
    const g = Math.tan((Math.PI * clamp(hz, 20, 0.47 * fs)) / fs);
    const k = 1 / clamp(q, 0.5, 45);
    const disc = Math.max(0, 1 - 0.25 * k * k);
    const sc = [-0.5 * g * k, g * Math.sqrt(disc)];
    const z = cDiv([1 + sc[0], sc[1]], [1 - sc[0], -sc[1]]);
    return {
      r: clamp(cAbs(z), 0, 0.999),
      theta: clamp(Math.abs(cArg(z)), 0, Math.PI),
    };
  };
  const p = bilinearRoot(stage.poleHz, stage.poleQ);
  const z = bilinearRoot(stage.zeroHz, stage.zeroQ);
  return { poleR: p.r, poleTheta: p.theta, zeroR: z.r, zeroTheta: z.theta };
}

// Inverse bilinear transform: drag (r, theta) on the Z-plane unit circle -> { hz, q }
export function zPointToHzQ(r, theta, sampleRate = SAMPLE_RATE) {
  const fs = Math.max(sampleRate, 22050);
  const rc = clamp(r, 0.08, 0.994);
  const tc = clamp(theta, 0.02, Math.PI - 0.04);
  const z = [rc * Math.cos(tc), rc * Math.sin(tc)];
  // s = (z - 1) / (z + 1)
  const s = cDiv([z[0] - 1, z[1]], [z[0] + 1, z[1]]);
  const g = Math.max(1e-4, cAbs(s));
  const hz = clamp((Math.atan(g) * fs) / Math.PI, 40, 0.45 * fs);
  const k = clamp((-2 * s[0]) / g, 1 / 42, 2.0);
  const q = clamp(1 / k, 0.5, 42);
  return { hz, q };
}

export function evaluateStageH(stage, hz, sampleRate = SAMPLE_RATE) {
  const fs = Math.max(sampleRate, 22050);
  const gEval = Math.tan((Math.PI * clamp(hz, 10, 0.48 * fs)) / fs);
  const gp = Math.tan((Math.PI * clamp(stage.poleHz, 20, 0.47 * fs)) / fs);
  const gz = Math.tan((Math.PI * clamp(stage.zeroHz, 20, 0.47 * fs)) / fs);
  const kp = 1 / clamp(stage.poleQ, 0.5, 45);
  const kz = 1 / clamp(stage.zeroQ, 0.5, 24);
  const r = gz / gp;
  const norm = 1 / Math.max(0.25, r);
  const w = gEval / gp;
  const s2 = -w * w;
  const num = [norm * (s2 + r * r), norm * (r * kz * w)];
  const den = [s2 + 1, kp * w];
  return cDiv(num, den);
}

export function evaluateMagnitudeDb(
  lattice,
  activeStages = NUM_STAGES,
  topology = 0,
  hz = 1000,
  sampleRate = SAMPLE_RATE
) {
  const n = clamp(activeStages, 1, lattice.stages.length);
  const fs = Math.max(sampleRate, 22050);
  const gEval = Math.tan((Math.PI * clamp(hz, 10, 0.48 * fs)) / fs);

  const stageBpH = (s) => {
    const gp = Math.tan((Math.PI * clamp(s.poleHz, 20, 0.47 * fs)) / fs);
    const kp = 1 / clamp(s.poleQ, 0.5, 45);
    const w = gEval / gp;
    return cDiv([0, Math.sqrt(kp) * w], [1 - w * w, kp * w]);
  };

  let total = [1, 0];
  if (topology === 0) {
    for (let i = 0; i < n; i++) total = cMul(total, evaluateStageH(lattice.stages[i], hz, fs));
  } else if (topology === 2) {
    total = [0, 0];
    const invSqrtN = 1 / Math.sqrt(n);
    for (let i = 0; i < n; i++) {
      const sign = (i & 1) === 0 ? invSqrtN : -invSqrtN;
      const h = stageBpH(lattice.stages[i]);
      total = cAdd(total, [sign * h[0], sign * h[1]]);
    }
  } else {
    let a = [1, 0];
    let b = [1, 0];
    for (let i = 0; i < n; i++) {
      if ((i & 1) === 0) a = cMul(a, evaluateStageH(lattice.stages[i], hz, fs));
      else b = cMul(b, evaluateStageH(lattice.stages[i], hz, fs));
    }
    total = [0.5 * (a[0] + b[0]), 0.5 * (a[1] + b[1])];
  }
  const mag = cAbs(total) * (lattice.makeupLinear ?? 1);
  return 20 * Math.log10(Math.max(mag, 1e-4));
}

export function resolveLattice({
  cubeIndex = 0,
  morphX = 0.25,
  morphY = 0.25,
  morphZ = 0.0,
  freqSemitones = 0,
  resAmount = 0.45,
  transform = 0,
  activeStages = 7,
  topology = 0,
  customCorner = null,
}) {
  const cube = FACTORY_CUBES[clamp(cubeIndex, 0, FACTORY_CUBES.length - 1)];
  const x = clamp(morphX, 0, 1);
  const y = clamp(morphY, 0, 1);
  const z = clamp(morphZ, 0, 1);
  const clampedRes = clamp(resAmount, 0, 1);
  const resScale = 0.3 * Math.pow(15.3, clampedRes);
  const xform = clamp(transform, -1, 1);
  const freqMul = Math.pow(2, freqSemitones / 12);

  const stages = [];
  let cornerMakeupDb = 0;

  if (customCorner) {
    cornerMakeupDb = customCorner.makeupDb ?? 0;
    for (let s = 0; s < NUM_STAGES; s++) {
      const st = customCorner.stages[s];
      const ratio = st.zeroHz / Math.max(st.poleHz, 20);
      const warpedRatio = Math.pow(ratio, 1 + 0.85 * xform) * Math.pow(2, 0.35 * xform);
      const finalPoleHz = clamp(st.poleHz * freqMul, 30, 18500);
      stages.push({
        poleHz: finalPoleHz,
        poleQ: clamp(st.poleQ * resScale, 0.55, 45),
        zeroHz: clamp(finalPoleHz * warpedRatio, 30, 19500),
        zeroQ: clamp(st.zeroQ * (0.55 + 0.75 * clampedRes), 0.55, 24),
      });
    }
  } else {
    const c = cube.corners;
    cornerMakeupDb = linTrilerp(
      c[0].makeupDb, c[1].makeupDb, c[2].makeupDb, c[3].makeupDb,
      c[4].makeupDb, c[5].makeupDb, c[6].makeupDb, c[7].makeupDb,
      x, y, z
    );
    for (let s = 0; s < NUM_STAGES; s++) {
      const pf = logTrilerp(
        c[0].stages[s].poleHz, c[1].stages[s].poleHz, c[2].stages[s].poleHz, c[3].stages[s].poleHz,
        c[4].stages[s].poleHz, c[5].stages[s].poleHz, c[6].stages[s].poleHz, c[7].stages[s].poleHz,
        x, y, z
      );
      const pq = logTrilerp(
        c[0].stages[s].poleQ, c[1].stages[s].poleQ, c[2].stages[s].poleQ, c[3].stages[s].poleQ,
        c[4].stages[s].poleQ, c[5].stages[s].poleQ, c[6].stages[s].poleQ, c[7].stages[s].poleQ,
        x, y, z
      );
      const zf = logTrilerp(
        c[0].stages[s].zeroHz, c[1].stages[s].zeroHz, c[2].stages[s].zeroHz, c[3].stages[s].zeroHz,
        c[4].stages[s].zeroHz, c[5].stages[s].zeroHz, c[6].stages[s].zeroHz, c[7].stages[s].zeroHz,
        x, y, z
      );
      const zq = logTrilerp(
        c[0].stages[s].zeroQ, c[1].stages[s].zeroQ, c[2].stages[s].zeroQ, c[3].stages[s].zeroQ,
        c[4].stages[s].zeroQ, c[5].stages[s].zeroQ, c[6].stages[s].zeroQ, c[7].stages[s].zeroQ,
        x, y, z
      );
      const ratio = zf / Math.max(pf, 20);
      const warpedRatio = Math.pow(ratio, 1 + 0.85 * xform) * Math.pow(2, 0.35 * xform);
      const finalPoleHz = clamp(pf * freqMul, 30, 18500);
      stages.push({
        poleHz: finalPoleHz,
        poleQ: clamp(pq * resScale, 0.55, 45),
        zeroHz: clamp(finalPoleHz * warpedRatio, 30, 19500),
        zeroQ: clamp(zq * (0.55 + 0.75 * clampedRes), 0.55, 24),
      });
    }
  }

  // Analytical passband auto-makeup across log-spaced probe frequencies
  const probes = [180, 360, 720, 1400, 2800, 5600, 9600];
  let sumSq = 0;
  const rawLattice = { stages, makeupLinear: 1 };
  for (const hz of probes) {
    const db = evaluateMagnitudeDb(rawLattice, activeStages, topology, hz, SAMPLE_RATE);
    const lin = Math.pow(10, clamp(db, -36, 24) / 20);
    sumSq += lin * lin;
  }
  const passbandEst = Math.sqrt(sumSq / probes.length);
  const maxBoost = topology === 2 ? 3.2 : 2.0;
  const autoGain = clamp(1 / Math.max(passbandEst, 1e-4), 0.15, maxBoost);
  const resTrimDb = (topology === 2 ? -2.2 : -4.8) * Math.max(0, clampedRes - 0.38);
  const fineTrim = Math.pow(10, (clamp(cornerMakeupDb, -6, 6) + resTrimDb) / 20);
  return { stages, makeupLinear: autoGain * fineTrim };
}

// Lab 2: Compare Naive Direct-Form Biquad (a1, a2) vs Log-Polar interpolation
// between two high-Q resonant poles (shows why naive interpolation collapses Q or explodes!)
export function compareCartesianVsPolar(
  t,
  start = { hz: 220, q: 26 },
  end = { hz: 5200, q: 26 },
  sampleRate = SAMPLE_RATE
) {
  const tc = clamp(t, 0, 1);
  const p0 = stageZPoint({ poleHz: start.hz, poleQ: start.q, zeroHz: start.hz, zeroQ: 1 }, sampleRate);
  const p1 = stageZPoint({ poleHz: end.hz, poleQ: end.q, zeroHz: end.hz, zeroQ: 1 }, sampleRate);

  // Direct-form denominator coefficients: 1 + a1 z^-1 + a2 z^-2, where a1 = -2 r cos(theta), a2 = r^2
  const a1_0 = -2 * p0.poleR * Math.cos(p0.poleTheta);
  const a2_0 = p0.poleR * p0.poleR;
  const a1_1 = -2 * p1.poleR * Math.cos(p1.poleTheta);
  const a2_1 = p1.poleR * p1.poleR;

  const a1 = a1_0 + (a1_1 - a1_0) * tc;
  const a2 = a2_0 + (a2_1 - a2_0) * tc;

  // Roots of z^2 + a1 z + a2 = 0
  const disc = a1 * a1 - 4 * a2;
  let dfR, dfTheta, dfHz, dfQ;
  if (disc < 0) {
    const re = -0.5 * a1;
    const im = 0.5 * Math.sqrt(-disc);
    dfR = Math.hypot(re, im);
    dfTheta = Math.atan2(im, re);
    const hzQ = zPointToHzQ(Math.min(dfR, 0.998), dfTheta, sampleRate);
    dfHz = hzQ.hz;
    dfQ = hzQ.q;
  } else {
    // Collapsed onto the real axis — zero resonant peak left!
    dfR = Math.max(Math.abs(-0.5 * a1 + 0.5 * Math.sqrt(disc)), Math.abs(-0.5 * a1 - 0.5 * Math.sqrt(disc)));
    dfTheta = 0;
    dfHz = 20;
    dfQ = 0.5;
  }

  // Also show naive Cartesian pole (x, y) linear lerp: straight chord through interior
  const x0 = p0.poleR * Math.cos(p0.poleTheta);
  const y0 = p0.poleR * Math.sin(p0.poleTheta);
  const x1 = p1.poleR * Math.cos(p1.poleTheta);
  const y1 = p1.poleR * Math.sin(p1.poleTheta);
  const cartX = x0 + (x1 - x0) * tc;
  const cartY = y0 + (y1 - y0) * tc;
  const cartR = Math.hypot(cartX, cartY);
  const cartTheta = Math.atan2(cartY, cartX);
  const cartHzQ = zPointToHzQ(Math.min(cartR, 0.998), Math.max(0.02, cartTheta), sampleRate);

  // Pole Field Log-Polar interpolation
  const polarHz = Math.exp(Math.log(start.hz) + (Math.log(end.hz) - Math.log(start.hz)) * tc);
  const polarQ = Math.exp(Math.log(start.q) + (Math.log(end.q) - Math.log(start.q)) * tc);
  const polarPt = stageZPoint({ poleHz: polarHz, poleQ: polarQ, zeroHz: polarHz, zeroQ: 1 }, sampleRate);

  return {
    directForm: { r: dfR, theta: dfTheta, hz: dfHz, q: dfQ, realAxisCollapse: disc >= 0 },
    cartesian: { r: cartR, theta: cartTheta, hz: cartHzQ.hz, q: cartHzQ.q },
    polar: { r: polarPt.poleR, theta: polarPt.poleTheta, hz: polarHz, q: polarQ },
  };
}

// Lab 4: Exact ADAA2 vs Naive waveshaping aliasing comparison
// Exact mirror of modules/dsp/include/hba/dsp/adaa.h (TanhShaper, HardClipShaper, Buchla259Shaper, Adaa2)
const DILOG_CHEB = [
  1.0763682799446204, 0.0816965805805103, 0.005814157140778893,
  0.0005371619814542347, 0.00005724704675186882, 0.00000667454612186042,
  8.276467342706163e-7, 1.0733156726357625e-7, 1.4400773006172251e-8,
  1.984441463006211e-9, 2.7939936783935892e-10, 4.004026739797458e-11,
];
function dilogPosHalf(y) {
  const t = 4 * y - 1;
  const twoT = 2 * t;
  let b1 = 0;
  let b2 = 0;
  for (let k = DILOG_CHEB.length - 1; k >= 1; k--) {
    const b0 = twoT * b1 - b2 + DILOG_CHEB[k];
    b2 = b1;
    b1 = b0;
  }
  return y * (t * b1 - b2 + DILOG_CHEB[0]);
}

function tanhAd1(u) {
  const a = Math.abs(u);
  if (a <= 0.05) {
    const u2 = u * u;
    return u2 * (0.5 - u2 / 12 + (u2 * u2) / 45);
  }
  return a + Math.log1p(Math.exp(-2 * a)) - Math.LN2;
}

function tanhAd2(u) {
  const a = Math.abs(u);
  if (a <= 0.05) {
    const u2 = u * u;
    return u * u2 * (1 / 6 - u2 / 60 + (u2 * u2) / 315);
  }
  const x = Math.exp(-2 * a);
  const lp = Math.log1p(x);
  const li2Neg = -dilogPosHalf(x / (1 + x)) - 0.5 * lp * lp;
  const val = 0.5 * a * a - Math.LN2 * a + (Math.PI * Math.PI) / 24 + 0.5 * li2Neg;
  return u < 0 ? -val : val;
}

function hardClipEval(u) {
  return u > 1 ? 1 : u < -1 ? -1 : u;
}
function hardClipAd1(u) {
  const a = Math.abs(u);
  return a <= 1 ? 0.5 * a * a : a - 0.5;
}
function hardClipAd2(u) {
  const a = Math.abs(u);
  const val = a <= 1 ? (a * a * a) / 6 : 0.5 * a * a - 0.5 * a + 1 / 6;
  return u < 0 ? -val : val;
}

const BUCHLA_CELLS = [
  [1.0, -2.0],
  [3.0, 2.02693442],
  [4.99, -2.09327472],
  [6.805, 1.94396598],
  [9.1, -1.21239624],
];

function buchlaEval(u) {
  const a = Math.abs(u);
  let y = -1.0;
  if (a < 15.4396936) {
    y = a;
    for (const [th, w] of BUCHLA_CELLS) {
      if (a > th) y += w * (a - th);
    }
  }
  return u < 0 ? -y : y;
}

function buchlaAd1(u) {
  const a = Math.abs(u);
  if (a >= 15.4396936) return 1.33347853 - (a - 15.4396936);
  let y = 0.5 * a * a;
  for (const [th, w] of BUCHLA_CELLS) {
    if (a > th) y += 0.5 * w * (a - th) * (a - th);
  }
  return y;
}

function buchlaAd2(u) {
  const a = Math.abs(u);
  let y;
  if (a >= 15.4396936) {
    const d = a - 15.4396936;
    y = 19.1552893 + 1.33347853 * d - 0.5 * d * d;
  } else {
    y = (a * a * a) / 6;
    for (const [th, w] of BUCHLA_CELLS) {
      if (a > th) {
        const d = a - th;
        y += (w * d * d * d) / 6;
      }
    }
  }
  return u < 0 ? -y : y;
}

function runShaperSeries(xArr, mode, shaper) {
  const n = xArr.length;
  const out = new Float64Array(n);
  const ev = shaper === "clip" ? hardClipEval : shaper === "buchla" ? buchlaEval : Math.tanh;
  const f1 = shaper === "clip" ? hardClipAd1 : shaper === "buchla" ? buchlaAd1 : tanhAd1;
  const f2 = shaper === "clip" ? hardClipAd2 : shaper === "buchla" ? buchlaAd2 : tanhAd2;

  if (mode === "naive") {
    for (let i = 0; i < n; i++) out[i] = ev(xArr[i]);
    return out;
  }

  // Exact Bilbao et al. DAFx-17 ADAA2 processor from modules/dsp/include/hba/dsp/adaa.h:296-324
  let x1 = xArr[0];
  let x2 = xArr[0];
  let f2_1 = f2(x1);
  let d1_1 = f1(x1);

  for (let i = 0; i < n; i++) {
    const x0 = xArr[i];
    const f2_0 = f2(x0);
    const diff01 = x0 - x1;
    const d1_0 = Math.abs(diff01) > 1e-5 ? (f2_0 - f2_1) / diff01 : f1(0.5 * (x0 + x1));
    const diff02 = x0 - x2;
    let y;
    if (Math.abs(diff02) > 1e-4) {
      y = (2 * (d1_0 - d1_1)) / diff02;
    } else {
      const xBar = 0.5 * (x0 + x2);
      const delta = xBar - x1;
      if (Math.abs(delta) > 1e-4) {
        const d1Bar = (f2(xBar) - f2_1) / delta;
        y = (2 * (f1(xBar) - d1Bar)) / delta;
      } else {
        y = ev((x0 + x1 + x2) / 3);
      }
    }
    out[i] = y;
    x2 = x1;
    x1 = x0;
    f2_1 = f2_0;
    d1_1 = d1_0;
  }
  return out;
}

// Simple radix-2 FFT for real signals (spectrum visualization)
function fftMagDb(signal) {
  const N = signal.length;
  const re = new Float64Array(N);
  const im = new Float64Array(N);
  // Blackman-Harris window for crisp -92 dB noise floor
  for (let i = 0; i < N; i++) {
    const a = (2 * Math.PI * i) / (N - 1);
    const w = 0.35875 - 0.48829 * Math.cos(a) + 0.14128 * Math.cos(2 * a) - 0.01168 * Math.cos(3 * a);
    re[i] = signal[i] * w;
  }
  for (let i = 1, j = 0; i < N; i++) {
    let bit = N >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      [re[i], re[j]] = [re[j], re[i]];
      [im[i], im[j]] = [im[j], im[i]];
    }
  }
  for (let len = 2; len <= N; len <<= 1) {
    const ang = (-2 * Math.PI) / len;
    const wLenRe = Math.cos(ang);
    const wLenIm = Math.sin(ang);
    for (let i = 0; i < N; i += len) {
      let wRe = 1;
      let wIm = 0;
      const half = len >> 1;
      for (let j = 0; j < half; j++) {
        const uRe = re[i + j];
        const uIm = im[i + j];
        const vRe = re[i + j + half] * wRe - im[i + j + half] * wIm;
        const vIm = re[i + j + half] * wIm + im[i + j + half] * wRe;
        re[i + j] = uRe + vRe;
        im[i + j] = uIm + vIm;
        re[i + j + half] = uRe - vRe;
        im[i + j + half] = uIm - vIm;
        const nextWRe = wRe * wLenRe - wIm * wLenIm;
        wIm = wRe * wLenIm + wIm * wLenRe;
        wRe = nextWRe;
      }
    }
  }
  const bins = new Float32Array(N / 2);
  let maxMag = 1e-9;
  for (let i = 0; i < N / 2; i++) {
    const m = Math.hypot(re[i], im[i]);
    if (m > maxMag) maxMag = m;
    bins[i] = m;
  }
  for (let i = 0; i < N / 2; i++) {
    bins[i] = 20 * Math.log10(Math.max(bins[i] / maxMag, 1e-5));
  }
  return bins;
}

export function evaluateAdaaSpectrum(drive = 5.5, freqHz = 2130, shaper = "buchla", oversample = 2) {
  const N = 2048;
  const warmup = 64;
  const fs = 48000;
  const os = oversample === 2 ? 2 : 1;

  // Coherent-ish test sine at 48 kHz for Naive 1x
  const x1 = new Float64Array(N + warmup);
  for (let i = 0; i < N + warmup; i++) {
    x1[i] = drive * Math.sin((2 * Math.PI * freqHz * i) / fs);
  }
  const naive = fftMagDb(runShaperSeries(x1, "naive", shaper).subarray(warmup));

  // ADAA2 (at 1x or 2x oversampled with 31-tap Blackman-windowed halfband decimation)
  let adaaTime;
  if (os === 2) {
    const fsOs = fs * 2;
    const xOs = new Float64Array((N + warmup) * 2 + 32);
    for (let i = 0; i < xOs.length; i++) {
      xOs[i] = drive * Math.sin((2 * Math.PI * freqHz * i) / fsOs);
    }
    const yOs = runShaperSeries(xOs, "adaa2", shaper);
    // 31-tap linear-phase sinc lowpass at 0.45 * Nyquist_base before 2:1 decimation
    const M = 31;
    const mid = (M - 1) / 2;
    const taps = new Float64Array(M);
    let sum = 0;
    for (let k = 0; k < M; k++) {
      const n = k - mid;
      const sinc = n === 0 ? 0.46 : Math.sin(Math.PI * 0.46 * n) / (Math.PI * n);
      const win = 0.42 - 0.5 * Math.cos((2 * Math.PI * k) / (M - 1)) + 0.08 * Math.cos((4 * Math.PI * k) / (M - 1));
      taps[k] = sinc * win;
      sum += taps[k];
    }
    for (let k = 0; k < M; k++) taps[k] /= sum;
    adaaTime = new Float64Array(N);
    for (let i = 0; i < N; i++) {
      const center = (i + warmup) * 2 + mid;
      let acc = 0;
      for (let k = 0; k < M; k++) acc += yOs[center + k - mid] * taps[k];
      adaaTime[i] = acc;
    }
  } else {
    adaaTime = runShaperSeries(x1, "adaa2", shaper).subarray(warmup);
  }
  const adaa = fftMagDb(adaaTime);

  // Measure worst non-harmonic alias peak between 300 Hz and 22.5 kHz
  // Exclude ±5 bins around every true harmonic (Blackman-Harris mainlobe half-width is 4 bins)
  const binHz = fs / N;
  const isHarmonicBin = (bin) => {
    const f = bin * binHz;
    for (let k = 1; k * freqHz < fs * 0.52; k++) {
      if (Math.abs(f - k * freqHz) < binHz * 5.2) return true;
    }
    return false;
  };
  let naiveAliasDb = -100;
  let adaaAliasDb = -100;
  const loBin = Math.ceil(300 / binHz);
  const hiBin = Math.floor(22500 / binHz);
  for (let b = loBin; b < hiBin; b++) {
    if (!isHarmonicBin(b)) {
      if (naive[b] > naiveAliasDb) naiveAliasDb = naive[b];
      if (adaa[b] > adaaAliasDb) adaaAliasDb = adaa[b];
    }
  }
  return { naive, adaa, binHz, naiveAliasDb, adaaAliasDb, reductionDb: naiveAliasDb - adaaAliasDb };
}

// Lab 5: Exact 14th-order Burg's Method LPC + Durand-Kerner complex root finder
// Mirrors products/pole-field/core/src/cubes.cpp:imprintFromAudio()
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

  // Simple 1-pole anti-alias decimation + gentle pre-emphasis + Hann window
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

  // 14th-order Burg's method
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

  // Durand-Kerner complex polynomial root solver
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

  // Place alternating dipole zeros (even above pole, odd below pole)
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

// Synthesize distinct physical test waveforms for the Burg LPC lab so users can test instantly
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
  // Excite resonant filter bank with a rich glottal/pulse train + slight deterministic noise
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

// Inline AudioWorkletProcessor code running Pole Field's exact 7-stage TPT SVF + passive OTA
// damping + ADAA saturation + LR4 Mono Sub-Lock directly on the browser's audio thread!
export const POLE_FIELD_WORKLET_CODE = `
class PoleFieldBenchProcessor extends AudioWorkletProcessor {
  constructor() {
    super();
    this.playing = false;
    this.source = "reese";
    this.mode = "lattice"; // "single" | "lattice" | "adaa"
    this.topology = 0;     // 0 serial, 1 hybrid, 2 parallel
    this.activeStages = 7;
    this.drive = 0.25;
    this.drivePos = 1;     // 0 pre, 1 inter, 2 post
    this.subLock = true;
    this.makeup = 1.0;
    this.adaaEnabled = true;
    this.adaaDrive = 5.0;
    this.adaaFreq = 2100;
    this.adaaShaper = "buchla";
    this.phases = [0, 0, 0, 0, 0];
    this.noiseSeed = 2463534242;
    this.tick = 0;

    // 7-stage SVF state (L & R)
    this.stL = Array.from({ length: 7 }, () => ({ ic1: 0, ic2: 0 }));
    this.stR = Array.from({ length: 7 }, () => ({ ic1: 0, ic2: 0 }));
    this.coefs = Array.from({ length: 7 }, () => ({
      g: 0.05, k: 0.25, a1: 0.9, a2: 0.05, m0: 1, m1: 0, m2: 0, bpGain: 0.5
    }));
    // Simple 2-pole LR4-style sub crossover state
    this.subLp1 = 0; this.subLp2 = 0;
    this.dcX = 0; this.dcY = 0;
    this.adaaPrevX = 0; this.adaaPrevF1 = 0;

    this.port.onmessage = (e) => {
      const msg = e.data || {};
      if (msg.type === "state") {
        if (typeof msg.playing === "boolean") this.playing = msg.playing;
        if (msg.source) this.source = msg.source;
        if (msg.mode) this.mode = msg.mode;
        if (typeof msg.topology === "number") this.topology = msg.topology;
        if (typeof msg.activeStages === "number") this.activeStages = msg.activeStages;
        if (typeof msg.drive === "number") this.drive = msg.drive;
        if (typeof msg.drivePos === "number") this.drivePos = msg.drivePos;
        if (typeof msg.subLock === "boolean") this.subLock = msg.subLock;
        if (typeof msg.makeup === "number") this.makeup = msg.makeup;
        if (typeof msg.adaaEnabled === "boolean") this.adaaEnabled = msg.adaaEnabled;
        if (typeof msg.adaaDrive === "number") this.adaaDrive = msg.adaaDrive;
        if (typeof msg.adaaFreq === "number") this.adaaFreq = msg.adaaFreq;
        if (msg.adaaShaper) this.adaaShaper = msg.adaaShaper;
        if (Array.isArray(msg.stages)) this.updateCoefs(msg.stages);
      }
    };
  }

  updateCoefs(stages) {
    const fs = sampleRate;
    for (let i = 0; i < 7; i++) {
      const s = stages[i] || { poleHz: 400, poleQ: 2, zeroHz: 400, zeroQ: 2 };
      const gp = Math.tan(Math.PI * Math.min(0.46 * fs, Math.max(20, s.poleHz)) / fs);
      const gz = Math.tan(Math.PI * Math.min(0.46 * fs, Math.max(20, s.zeroHz)) / fs);
      const kp = 1 / Math.min(45, Math.max(0.5, s.poleQ));
      const kz = 1 / Math.min(24, Math.max(0.5, s.zeroQ));
      const r = gz / gp;
      const norm = 1 / Math.max(0.25, r);
      const a1 = 1 / (1 + gp * (gp + kp));
      this.coefs[i] = {
        g: gp, k: kp, a1, a2: gp * a1,
        m0: norm, m1: norm * (r * kz - kp), m2: norm * (r * r - 1),
        bpGain: Math.sqrt(kp)
      };
    }
  }

  softGuard(y) {
    const a = Math.abs(y);
    if (a <= 2.4) return y;
    const g = 2.4 + 1.6 * Math.tanh((a - 2.4) / 1.6);
    return y < 0 ? -g : g;
  }

  sat(x, amount) {
    if (amount <= 0.001) return x;
    const g = 1 + amount * 6.5;
    return Math.tanh(x * g) / Math.tanh(g);
  }

  buchlaEval(u) {
    const a = Math.abs(u);
    let y = -1.0;
    if (a < 15.4396936) {
      y = a;
      if (a > 1.0) y += -2.0 * (a - 1.0);
      if (a > 3.0) y += 2.02693442 * (a - 3.0);
      if (a > 4.99) y += -2.09327472 * (a - 4.99);
      if (a > 6.805) y += 1.94396598 * (a - 6.805);
      if (a > 9.1) y += -1.21239624 * (a - 9.1);
    }
    return u < 0 ? -y : y;
  }

  buchlaAd1(u) {
    const a = Math.abs(u);
    if (a >= 15.4396936) return 1.33347853 - (a - 15.4396936);
    let y = 0.5 * a * a;
    if (a > 1.0) y += -1.0 * (a - 1.0) * (a - 1.0);
    if (a > 3.0) y += 1.01346721 * (a - 3.0) * (a - 3.0);
    if (a > 4.99) y += -1.04663736 * (a - 4.99) * (a - 4.99);
    if (a > 6.805) y += 0.97198299 * (a - 6.805) * (a - 6.805);
    if (a > 9.1) y += -0.60619812 * (a - 9.1) * (a - 9.1);
    return y;
  }

  tickSvf(st, c, input) {
    const v3 = input - st.ic2;
    const v1Lin = c.a1 * st.ic1 + c.a2 * v3;
    // Exact Pole Field passive nonlinear OTA resonance damping (pole_zero.h:84-92)
    const e = v1Lin * (1 / 3.2);
    const kAdd = 0.28 * (e * e) / (1 + e * e);
    const a1 = 1 / (1 + c.g * (c.g + c.k + kAdd));
    const a2 = c.g * a1;
    const a3 = c.g * a2;
    const v1 = a1 * st.ic1 + a2 * v3;
    const v2 = st.ic2 + a2 * st.ic1 + a3 * v3;
    st.ic1 = 2 * v1 - st.ic1;
    st.ic2 = 2 * v2 - st.ic2;
    return [v1, v2];
  }

  genSource() {
    const fs = sampleRate;
    this.tick++;
    if (this.source === "chord") {
      // Dark minor-9th detuned saw pad (F1, C2, G#2, G3)
      const freqs = [43.65, 65.41, 103.83, 196.0, 196.8];
      let s = 0;
      for (let i = 0; i < 5; i++) {
        this.phases[i] = (this.phases[i] + freqs[i] / fs) % 1;
        s += (2 * this.phases[i] - 1) * 0.22;
      }
      return [s, s];
    }
    if (this.source === "drums") {
      // Punchy 172 BPM breakbeat pulse + noise transient
      const beatSamples = Math.floor((fs * 60) / 172);
      const step = Math.floor((this.tick % (beatSamples * 4)) / (beatSamples * 0.5));
      const pos = (this.tick % Math.floor(beatSamples * 0.5)) / (beatSamples * 0.5);
      this.noiseSeed = (this.noiseSeed * 1664525 + 1013904223) >>> 0;
      const noise = (this.noiseSeed / 4294967296) * 2 - 1;
      let sig = 0;
      if (step === 0 || step === 5) {
        const pitch = 52 + 120 * Math.exp(-pos * 18);
        this.phases[0] = (this.phases[0] + pitch / fs) % 1;
        sig += Math.sin(2 * Math.PI * this.phases[0]) * Math.exp(-pos * 6.5) * 0.9;
      }
      if (step === 2 || step === 6) {
        this.phases[1] = (this.phases[1] + 205 / fs) % 1;
        sig += (Math.sin(2 * Math.PI * this.phases[1]) * 0.55 + noise * 0.65) * Math.exp(-pos * 9.5);
      }
      sig += noise * Math.exp(-pos * 24) * 0.25;
      return [sig * 0.7, sig * 0.7];
    }
    // Default: Sub-heavy detuned F1 (43.65 Hz) Neuro Reese Bass
    const base = 43.6535;
    const det = 0.0085;
    const fL = base * (1 - det);
    const fR = base * (1 + det);
    this.phases[0] = (this.phases[0] + base / fs) % 1;
    this.phases[1] = (this.phases[1] + fL / fs) % 1;
    this.phases[2] = (this.phases[2] + fR / fs) % 1;
    this.phases[3] = (this.phases[3] + base * 2.004 / fs) % 1;
    const sub = Math.sin(2 * Math.PI * this.phases[0]) * 0.35;
    const sawL = (2 * this.phases[1] - 1) * 0.38 + (2 * this.phases[3] - 1) * 0.22;
    const sawR = (2 * this.phases[2] - 1) * 0.38 + (2 * this.phases[3] - 1) * 0.22;
    return [sub + sawL, sub + sawR];
  }

  processLattice(x, states) {
    let sig = x;
    if (this.drivePos === 0) sig = this.sat(sig, this.drive);
    const n = this.mode === "single" ? 1 : this.activeStages;
    const stageScale = Math.pow(Math.max(this.makeup, 0.05), 1 / Math.max(n, 1));

    let out = 0;
    if (this.mode === "single" || this.topology === 0) {
      for (let i = 0; i < n; i++) {
        const c = this.coefs[i];
        const [v1, v2] = this.tickSvf(states[i], c, sig);
        sig = this.softGuard((c.m0 * sig + c.m1 * v1 + c.m2 * v2) * stageScale);
        if (this.mode === "lattice" && this.drivePos === 1 && (i === 1 || i === 4)) {
          sig = this.sat(sig, this.drive * 0.8);
        }
      }
      out = sig;
    } else if (this.topology === 2) {
      let evenSum = 0, oddSum = 0;
      const invSqrt = 1 / Math.sqrt(n);
      for (let i = 0; i < n; i++) {
        const c = this.coefs[i];
        const [v1] = this.tickSvf(states[i], c, sig);
        const band = this.softGuard(c.bpGain * v1);
        if ((i & 1) === 0) evenSum += band;
        else oddSum += band;
      }
      evenSum *= invSqrt * this.makeup;
      oddSum *= invSqrt * this.makeup;
      if (this.drivePos === 1) {
        evenSum = this.sat(evenSum, this.drive * 0.85);
        oddSum = this.sat(oddSum, this.drive * 0.85);
      }
      out = evenSum - oddSum;
    } else {
      let a = sig, b = sig;
      const halfScale = Math.pow(Math.max(this.makeup, 0.05), 1 / Math.max(n * 0.5, 1));
      for (let i = 0; i < n; i++) {
        const c = this.coefs[i];
        if ((i & 1) === 0) {
          const [v1, v2] = this.tickSvf(states[i], c, a);
          a = this.softGuard((c.m0 * a + c.m1 * v1 + c.m2 * v2) * halfScale);
          if (this.drivePos === 1 && i === 2) a = this.sat(a, this.drive * 0.8);
        } else {
          const [v1, v2] = this.tickSvf(states[i], c, b);
          b = this.softGuard((c.m0 * b + c.m1 * v1 + c.m2 * v2) * halfScale);
          if (this.drivePos === 1 && i === 3) b = this.sat(b, this.drive * 0.8);
        }
      }
      out = 0.5 * (a + b);
    }

    if (this.drivePos === 2) out = this.sat(out, this.drive);
    return out;
  }

  process(_inputs, outputs) {
    const outL = outputs[0][0];
    const outR = outputs[0][1] || outL;
    if (!this.playing) {
      outL.fill(0);
      if (outR !== outL) outR.fill(0);
      return true;
    }

    const subAlpha = 1 - Math.exp((-2 * Math.PI * 105) / sampleRate);

    for (let i = 0; i < outL.length; i++) {
      if (this.mode === "adaa") {
        // High-frequency sweep sine to hear aliasing foldback clearly!
        this.phases[4] = (this.phases[4] + this.adaaFreq / sampleRate) % 1;
        const x0 = this.adaaDrive * Math.sin(2 * Math.PI * this.phases[4]);
        let y;
        if (!this.adaaEnabled) {
          y = this.adaaShaper === "buchla" ? this.buchlaEval(x0) : Math.tanh(x0);
        } else {
          const f1_0 = this.adaaShaper === "buchla"
            ? this.buchlaAd1(x0)
            : Math.abs(x0) + Math.log1p(Math.exp(-2 * Math.abs(x0))) - Math.LN2;
          const d = x0 - this.adaaPrevX;
          y = Math.abs(d) > 1e-4
            ? (f1_0 - this.adaaPrevF1) / d
            : (this.adaaShaper === "buchla" ? this.buchlaEval(0.5 * (x0 + this.adaaPrevX)) : Math.tanh(0.5 * (x0 + this.adaaPrevX)));
          this.adaaPrevX = x0;
          this.adaaPrevF1 = f1_0;
        }
        const s = Math.tanh(y * 0.45) * 0.28;
        outL[i] = s;
        if (outR !== outL) outR[i] = s;
        continue;
      }

      const [inL, inR] = this.genSource();
      const mono = 0.5 * (inL + inR);
      this.subLp1 += subAlpha * (mono - this.subLp1);
      this.subLp2 += subAlpha * (this.subLp1 - this.subLp2);
      const sub = this.subLp2;

      const hiL = this.subLock ? inL - sub : inL;
      const hiR = this.subLock ? inR - sub : inR;

      let yL = this.processLattice(hiL, this.stL);
      let yR = this.processLattice(hiR, this.stR);

      if (this.subLock) {
        yL += sub * 1.1;
        yR += sub * 1.1;
      }

      // Gentle DC block & safety ceiling
      const dcOut = yL - this.dcX + 0.996 * this.dcY;
      this.dcX = yL;
      this.dcY = dcOut;
      outL[i] = Math.tanh(dcOut * 0.52) * 0.65;
      if (outR !== outL) outR[i] = Math.tanh(yR * 0.52) * 0.65;
    }
    return true;
  }
}
registerProcessor("pole-field-bench", PoleFieldBenchProcessor);
`;
