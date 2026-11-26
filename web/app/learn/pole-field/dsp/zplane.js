// Z-plane bilinear geometry, log-polar trilinear cube morphing, and transfer-function evaluation.
import { FACTORY_CUBES, NUM_STAGES, SAMPLE_RATE } from "./cubes.js";

export const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
export const cAdd = (a, b) => [a[0] + b[0], a[1] + b[1]];
export const cSub = (a, b) => [a[0] - b[0], a[1] - b[1]];
export const cMul = (a, b) => [a[0] * b[0] - a[1] * b[1], a[0] * b[1] + a[1] * b[0]];
export const cDiv = (a, b) => {
  const d = b[0] * b[0] + b[1] * b[1] + 1e-30;
  return [(a[0] * b[0] + a[1] * b[1]) / d, (a[1] * b[0] - a[0] * b[1]) / d];
};
export const cAbs = (a) => Math.hypot(a[0], a[1]);
export const cArg = (a) => Math.atan2(a[1], a[0]);

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
      z,
    ),
  );
}

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

export function zPointToHzQ(r, theta, sampleRate = SAMPLE_RATE) {
  const fs = Math.max(sampleRate, 22050);
  const rc = clamp(r, 0.08, 0.994);
  const tc = clamp(theta, 0.02, Math.PI - 0.04);
  const z = [rc * Math.cos(tc), rc * Math.sin(tc)];
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
  sampleRate = SAMPLE_RATE,
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
      x, y, z,
    );
    for (let s = 0; s < NUM_STAGES; s++) {
      const pf = logTrilerp(
        c[0].stages[s].poleHz, c[1].stages[s].poleHz, c[2].stages[s].poleHz, c[3].stages[s].poleHz,
        c[4].stages[s].poleHz, c[5].stages[s].poleHz, c[6].stages[s].poleHz, c[7].stages[s].poleHz,
        x, y, z,
      );
      const pq = logTrilerp(
        c[0].stages[s].poleQ, c[1].stages[s].poleQ, c[2].stages[s].poleQ, c[3].stages[s].poleQ,
        c[4].stages[s].poleQ, c[5].stages[s].poleQ, c[6].stages[s].poleQ, c[7].stages[s].poleQ,
        x, y, z,
      );
      const zf = logTrilerp(
        c[0].stages[s].zeroHz, c[1].stages[s].zeroHz, c[2].stages[s].zeroHz, c[3].stages[s].zeroHz,
        c[4].stages[s].zeroHz, c[5].stages[s].zeroHz, c[6].stages[s].zeroHz, c[7].stages[s].zeroHz,
        x, y, z,
      );
      const zq = logTrilerp(
        c[0].stages[s].zeroQ, c[1].stages[s].zeroQ, c[2].stages[s].zeroQ, c[3].stages[s].zeroQ,
        c[4].stages[s].zeroQ, c[5].stages[s].zeroQ, c[6].stages[s].zeroQ, c[7].stages[s].zeroQ,
        x, y, z,
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

export function compareCartesianVsPolar(
  t,
  start = { hz: 220, q: 26 },
  end = { hz: 5200, q: 26 },
  sampleRate = SAMPLE_RATE,
) {
  const tc = clamp(t, 0, 1);
  const p0 = stageZPoint({ poleHz: start.hz, poleQ: start.q, zeroHz: start.hz, zeroQ: 1 }, sampleRate);
  const p1 = stageZPoint({ poleHz: end.hz, poleQ: end.q, zeroHz: end.hz, zeroQ: 1 }, sampleRate);

  const a1_0 = -2 * p0.poleR * Math.cos(p0.poleTheta);
  const a2_0 = p0.poleR * p0.poleR;
  const a1_1 = -2 * p1.poleR * Math.cos(p1.poleTheta);
  const a2_1 = p1.poleR * p1.poleR;

  const a1 = a1_0 + (a1_1 - a1_0) * tc;
  const a2 = a2_0 + (a2_1 - a2_0) * tc;

  const disc = a1 * a1 - 4 * a2;
  let dfR;
  let dfTheta;
  let dfHz;
  let dfQ;
  if (disc < 0) {
    const re = -0.5 * a1;
    const im = 0.5 * Math.sqrt(-disc);
    dfR = Math.hypot(re, im);
    dfTheta = Math.atan2(im, re);
    const hzQ = zPointToHzQ(Math.min(dfR, 0.998), dfTheta, sampleRate);
    dfHz = hzQ.hz;
    dfQ = hzQ.q;
  } else {
    dfR = Math.max(
      Math.abs(-0.5 * a1 + 0.5 * Math.sqrt(disc)),
      Math.abs(-0.5 * a1 - 0.5 * Math.sqrt(disc)),
    );
    dfTheta = 0;
    dfHz = 20;
    dfQ = 0.5;
  }

  const x0 = p0.poleR * Math.cos(p0.poleTheta);
  const y0 = p0.poleR * Math.sin(p0.poleTheta);
  const x1 = p1.poleR * Math.cos(p1.poleTheta);
  const y1 = p1.poleR * Math.sin(p1.poleTheta);
  const cartX = x0 + (x1 - x0) * tc;
  const cartY = y0 + (y1 - y0) * tc;
  const cartR = Math.hypot(cartX, cartY);
  const cartTheta = Math.atan2(cartY, cartX);
  const cartHzQ = zPointToHzQ(Math.min(cartR, 0.998), Math.max(0.02, cartTheta), sampleRate);

  const polarHz = Math.exp(Math.log(start.hz) + (Math.log(end.hz) - Math.log(start.hz)) * tc);
  const polarQ = Math.exp(Math.log(start.q) + (Math.log(end.q) - Math.log(start.q)) * tc);
  const polarPt = stageZPoint({ poleHz: polarHz, poleQ: polarQ, zeroHz: polarHz, zeroQ: 1 }, sampleRate);

  return {
    directForm: { r: dfR, theta: dfTheta, hz: dfHz, q: dfQ, realAxisCollapse: disc >= 0 },
    cartesian: { r: cartR, theta: cartTheta, hz: cartHzQ.hz, q: cartHzQ.q },
    polar: { r: polarPt.poleR, theta: polarPt.poleTheta, hz: polarHz, q: polarQ },
  };
}
