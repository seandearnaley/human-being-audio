// ADAA2 anti-aliased waveshapers & FFT aliasing analyzer, mirroring modules/dsp/include/hba/dsp/adaa.h.

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

export const BUCHLA_CELLS = [
  [1.0, -2.0],
  [3.0, 2.02693442],
  [4.99, -2.09327472],
  [6.805, 1.94396598],
  [9.1, -1.21239624],
];

export function tanhAd1(u) {
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

export function hardClipEval(u) {
  return u > 1 ? 1 : u < -1 ? -1 : u;
}

export function hardClipAd1(u) {
  const a = Math.abs(u);
  return a <= 1 ? 0.5 * a * a : a - 0.5;
}

function hardClipAd2(u) {
  const a = Math.abs(u);
  const val = a <= 1 ? (a * a * a) / 6 : 0.5 * a * a - 0.5 * a + 1 / 6;
  return u < 0 ? -val : val;
}

export function buchlaEval(u) {
  const a = Math.abs(u);
  let y = -1.0;
  if (a < 15.4396936) {
    y = a;
    for (let i = 0; i < BUCHLA_CELLS.length; i++) {
      const th = BUCHLA_CELLS[i][0];
      const w = BUCHLA_CELLS[i][1];
      if (a > th) y += w * (a - th);
    }
  }
  return u < 0 ? -y : y;
}

export function buchlaAd1(u) {
  const a = Math.abs(u);
  if (a >= 15.4396936) return 1.33347853 - (a - 15.4396936);
  let y = 0.5 * a * a;
  for (let i = 0; i < BUCHLA_CELLS.length; i++) {
    const th = BUCHLA_CELLS[i][0];
    const w = BUCHLA_CELLS[i][1];
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
    for (let i = 0; i < BUCHLA_CELLS.length; i++) {
      const th = BUCHLA_CELLS[i][0];
      const w = BUCHLA_CELLS[i][1];
      if (a > th) {
        const d = a - th;
        y += (w * d * d * d) / 6;
      }
    }
  }
  return u < 0 ? -y : y;
}

// Explicit unminified shaper formulas embedded into the AudioWorklet Blob.
// Never derive this via Function.prototype.toString(), because production minifiers rename
// function names and closed-over constants (breaking AudioWorklet scope with ReferenceError).
export const WORKLET_SHAPER_SOURCE = `
const DILOG_CHEB = ${JSON.stringify(DILOG_CHEB)};
const BUCHLA_CELLS = ${JSON.stringify(BUCHLA_CELLS)};

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

function buchlaEval(u) {
  const a = Math.abs(u);
  let y = -1.0;
  if (a < 15.4396936) {
    y = a;
    for (let i = 0; i < BUCHLA_CELLS.length; i++) {
      const th = BUCHLA_CELLS[i][0];
      const w = BUCHLA_CELLS[i][1];
      if (a > th) y += w * (a - th);
    }
  }
  return u < 0 ? -y : y;
}

function buchlaAd1(u) {
  const a = Math.abs(u);
  if (a >= 15.4396936) return 1.33347853 - (a - 15.4396936);
  let y = 0.5 * a * a;
  for (let i = 0; i < BUCHLA_CELLS.length; i++) {
    const th = BUCHLA_CELLS[i][0];
    const w = BUCHLA_CELLS[i][1];
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
    for (let i = 0; i < BUCHLA_CELLS.length; i++) {
      const th = BUCHLA_CELLS[i][0];
      const w = BUCHLA_CELLS[i][1];
      if (a > th) {
        const d = a - th;
        y += (w * d * d * d) / 6;
      }
    }
  }
  return u < 0 ? -y : y;
}
`;

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

function fftMagDb(signal) {
  const N = signal.length;
  const re = new Float64Array(N);
  const im = new Float64Array(N);
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

  const x1 = new Float64Array(N + warmup);
  for (let i = 0; i < N + warmup; i++) {
    x1[i] = drive * Math.sin((2 * Math.PI * freqHz * i) / fs);
  }
  const naive = fftMagDb(runShaperSeries(x1, "naive", shaper).subarray(warmup));

  let adaaTime;
  if (os === 2) {
    const fsOs = fs * 2;
    const xOs = new Float64Array((N + warmup) * 2 + 32);
    for (let i = 0; i < xOs.length; i++) {
      xOs[i] = drive * Math.sin((2 * Math.PI * freqHz * i) / fsOs);
    }
    const yOs = runShaperSeries(xOs, "adaa2", shaper);
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
