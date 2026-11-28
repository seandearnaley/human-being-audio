// AudioWorkletProcessor code for the live Pole Field Web Audio benches, sharing shaper formulas from adaa.js.
import { WORKLET_SHAPER_SOURCE } from "./adaa.js";

export const POLE_FIELD_WORKLET_CODE = `
${WORKLET_SHAPER_SOURCE}

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
    this.adaaOs = 1;
    this.phases = [0, 0, 0, 0, 0];
    this.noiseSeed = 2463534242;
    this.tick = 0;

    this.stL = Array.from({ length: 7 }, () => ({ ic1: 0, ic2: 0 }));
    this.stR = Array.from({ length: 7 }, () => ({ ic1: 0, ic2: 0 }));
    this.coefs = Array.from({ length: 7 }, () => ({
      g: 0.05, k: 0.25, a1: 0.9, a2: 0.05, m0: 1, m1: 0, m2: 0, bpGain: 0.5
    }));
    this.subLp1 = 0; this.subLp2 = 0;
    this.dcX = 0; this.dcY = 0;
    this.dcXR = 0; this.dcYR = 0;
    this.adaaX1 = 0; this.adaaX2 = 0;
    this.adaaF2_1 = 0; this.adaaD1_1 = 0;

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
        if (msg.adaaShaper && msg.adaaShaper !== this.adaaShaper) {
          this.adaaShaper = msg.adaaShaper;
          this.adaaX1 = 0; this.adaaX2 = 0;
          this.adaaF2_1 = 0; this.adaaD1_1 = 0;
        }
        if (typeof msg.adaaOs === "number") this.adaaOs = msg.adaaOs === 2 ? 2 : 1;
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

  tickSvf(st, c, input) {
    const v3 = input - st.ic2;
    const v1Lin = c.a1 * st.ic1 + c.a2 * v3;
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
      const freqs = [43.65, 65.41, 103.83, 196.0, 196.8];
      let s = 0;
      for (let i = 0; i < 5; i++) {
        this.phases[i] = (this.phases[i] + freqs[i] / fs) % 1;
        s += (2 * this.phases[i] - 1) * 0.22;
      }
      return [s, s];
    }
    if (this.source === "drums") {
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

  shaperEval(u) {
    if (this.adaaShaper === "buchla") return buchlaEval(u);
    if (this.adaaShaper === "clip") return hardClipEval(u);
    return Math.tanh(u);
  }

  shaperAd1(u) {
    if (this.adaaShaper === "buchla") return buchlaAd1(u);
    if (this.adaaShaper === "clip") return hardClipAd1(u);
    return tanhAd1(u);
  }

  shaperAd2(u) {
    if (this.adaaShaper === "buchla") return buchlaAd2(u);
    if (this.adaaShaper === "clip") return hardClipAd2(u);
    return tanhAd2(u);
  }

  stepAdaaSample(x0) {
    if (!this.adaaEnabled) return this.shaperEval(x0);
    const f2_0 = this.shaperAd2(x0);
    const diff01 = x0 - this.adaaX1;
    const d1_0 = Math.abs(diff01) > 1e-5
      ? (f2_0 - this.adaaF2_1) / diff01
      : this.shaperAd1(0.5 * (x0 + this.adaaX1));
    const diff02 = x0 - this.adaaX2;
    let y;
    if (Math.abs(diff02) > 1e-4) {
      y = (2 * (d1_0 - this.adaaD1_1)) / diff02;
    } else {
      const xBar = 0.5 * (x0 + this.adaaX2);
      const delta = xBar - this.adaaX1;
      if (Math.abs(delta) > 1e-4) {
        const d1Bar = (this.shaperAd2(xBar) - this.adaaF2_1) / delta;
        y = (2 * (this.shaperAd1(xBar) - d1Bar)) / delta;
      } else {
        y = this.shaperEval((x0 + this.adaaX1 + this.adaaX2) / 3);
      }
    }
    this.adaaX2 = this.adaaX1;
    this.adaaX1 = x0;
    this.adaaF2_1 = f2_0;
    this.adaaD1_1 = d1_0;
    return y;
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
        const inc = this.adaaFreq / sampleRate;
        let y = 0;
        if (this.adaaOs === 2) {
          this.phases[4] = (this.phases[4] + 0.5 * inc) % 1;
          const y0 = this.stepAdaaSample(this.adaaDrive * Math.sin(2 * Math.PI * this.phases[4]));
          this.phases[4] = (this.phases[4] + 0.5 * inc) % 1;
          const y1 = this.stepAdaaSample(this.adaaDrive * Math.sin(2 * Math.PI * this.phases[4]));
          y = 0.5 * (y0 + y1);
        } else {
          this.phases[4] = (this.phases[4] + inc) % 1;
          y = this.stepAdaaSample(this.adaaDrive * Math.sin(2 * Math.PI * this.phases[4]));
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

      const dcOutL = yL - this.dcX + 0.996 * this.dcY;
      this.dcX = yL;
      this.dcY = dcOutL;
      const dcOutR = yR - this.dcXR + 0.996 * this.dcYR;
      this.dcXR = yR;
      this.dcYR = dcOutR;
      outL[i] = Math.tanh(dcOutL * 0.52) * 0.65;
      if (outR !== outL) outR[i] = Math.tanh(dcOutR * 0.52) * 0.65;
    }
    return true;
  }
}
registerProcessor("pole-field-bench", PoleFieldBenchProcessor);
`;
