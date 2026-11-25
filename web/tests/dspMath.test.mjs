import test from "node:test";
import assert from "node:assert/strict";
import {
  FACTORY_CUBES,
  SAMPLE_RATE,
  compareCartesianVsPolar,
  evaluateAdaaSpectrum,
  evaluateMagnitudeDb,
  generateSyntheticImprintSource,
  resolveLattice,
  runBurgLpcImprint,
  stageZPoint,
  zPointToHzQ,
} from "../app/learn/pole-field/dspMath.js";

test("all factory cube corners resolve to strictly stable Z-plane poles inside |z| < 1", () => {
  for (let c = 0; c < FACTORY_CUBES.length; c++) {
    for (const [x, y, z] of [
      [0, 0, 0],
      [1, 0, 0],
      [0, 1, 0],
      [1, 1, 1],
      [0.37, 0.62, 0.45],
    ]) {
      const lat = resolveLattice({ cubeIndex: c, morphX: x, morphY: y, morphZ: z, resAmount: 0.7 });
      assert.equal(lat.stages.length, 7);
      assert.ok(Number.isFinite(lat.makeupLinear) && lat.makeupLinear > 0);
      for (const st of lat.stages) {
        const pt = stageZPoint(st, SAMPLE_RATE);
        assert.ok(pt.poleR > 0 && pt.poleR < 1, `unstable pole radius ${pt.poleR}`);
        assert.ok(pt.poleTheta > 0 && pt.poleTheta < Math.PI);
      }
      const mag = evaluateMagnitudeDb(lat, 7, 0, 1000, SAMPLE_RATE);
      assert.ok(Number.isFinite(mag) && mag > -80 && mag < 48);
    }
  }
});

test("inverse bilinear transform zPointToHzQ round-trips stageZPoint accurately", () => {
  const src = { poleHz: 1420, poleQ: 8.5, zeroHz: 2100, zeroQ: 5.0 };
  const pt = stageZPoint(src, SAMPLE_RATE);
  const back = zPointToHzQ(pt.poleR, pt.poleTheta, SAMPLE_RATE);
  assert.ok(Math.abs(back.hz - src.poleHz) < 15, `hz roundtrip drift: ${back.hz}`);
  assert.ok(Math.abs(back.q - src.poleQ) < 0.35, `q roundtrip drift: ${back.q}`);
});

test("log-polar interpolation preserves resonance while Cartesian collapses Q at midpoint", () => {
  const mid = compareCartesianVsPolar(0.5, { hz: 220, q: 26 }, { hz: 5200, q: 26 }, SAMPLE_RATE);
  assert.ok(Math.abs(mid.polar.q - 26) < 0.05);
  assert.ok(mid.cartesian.q < 8, `expected Cartesian Q collapse, got ${mid.cartesian.q}`);
  assert.ok(mid.polar.r > mid.cartesian.r);
});

test("ADAA reduces worst inharmonic aliasing peak on driven Buchla wavefolder", () => {
  const res = evaluateAdaaSpectrum(6.0, 2130, "buchla");
  assert.ok(res.reductionDb > 6, `expected strong ADAA alias suppression, got ${res.reductionDb} dB`);
});

test("14th-order Burg LPC extracts 7 sorted, strictly stable formant dipole stages", () => {
  for (const kind of ["vowel", "snare", "brass", "bell"]) {
    const { samples, sampleRate } = generateSyntheticImprintSource(kind);
    const corner = runBurgLpcImprint(samples, sampleRate);
    assert.equal(corner.stages.length, 7);
    for (let i = 0; i < 7; i++) {
      const s = corner.stages[i];
      assert.ok(Number.isFinite(s.poleHz) && s.poleHz >= 55 && s.poleHz <= 14000);
      assert.ok(Number.isFinite(s.poleQ) && s.poleQ >= 1.3 && s.poleQ <= 12);
      if (i > 0) assert.ok(s.poleHz >= corner.stages[i - 1].poleHz);
      const pt = stageZPoint(s, SAMPLE_RATE);
      assert.ok(pt.poleR < 1);
    }
  }
});
