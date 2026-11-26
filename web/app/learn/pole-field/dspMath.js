// Barrel module re-exporting Pole Field's JavaScript DSP core.
export { FACTORY_CUBES, NUM_STAGES, SAMPLE_RATE } from "./dsp/cubes.js";
export {
  compareCartesianVsPolar,
  evaluateMagnitudeDb,
  evaluateStageH,
  resolveLattice,
  stageZPoint,
  zPointToHzQ,
} from "./dsp/zplane.js";
export { evaluateAdaaSpectrum } from "./dsp/adaa.js";
export { generateSyntheticImprintSource, runBurgLpcImprint } from "./dsp/lpc.js";
export { POLE_FIELD_WORKLET_CODE } from "./dsp/worklet.js";
