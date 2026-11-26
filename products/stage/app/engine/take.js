// A synthetic take: a 4/4 two-step break. Kick on beat 1 and the "and" of 3, snare on 2 and 4,
// hats on every eighth. Every value is a pure function of musical position, so a frame can be
// produced for any time, in any order, at any tempo or frame rate.
const KICKS = [0, 2.5];
const SNARES = [1, 3];

const sinceHit = (pos, hits, len) => Math.min(...hits.map((h) => (((pos - h) % len) + len) % len));
const decay = (beats, rate) => Math.exp(-beats * rate);
const clamp01 = (v) => Math.min(1, Math.max(0, v));
const bump = (x, centre, width) => Math.exp(-(((x - centre) / width) ** 2));

// A stand-in spectrum shaped from three band levels, until the plugin sends a real one.
export function spectrumFor({ low, mid, high, barIndex }) {
  const bands = Float32Array.from([low, low * 0.8, low * 0.4 + mid * 0.4, mid * 0.9, mid, mid * 0.6 + high * 0.3, high, high * 0.8], clamp01);
  const spectrum = Float32Array.from({ length: 64 }, (_, i) => {
    const x = i / 63;
    const shape = low * bump(x, 0.05, 0.09) + mid * bump(x, 0.4, 0.16) + high * bump(x, 0.85, 0.13);
    return clamp01(shape * (0.75 + 0.25 * Math.sin(i * 1.7 + barIndex)));
  });
  return { bands, spectrum };
}

export function frameAtTime(t, { bpm = 174, sig = [4, 4], width = 1920, height = 1080, seed = 1, fps = 60, n = Math.round(t * fps), kicks = KICKS, snares = SNARES } = {}) {
  const beat = (t * bpm) / 60;
  const beatsPerBar = sig[0];
  const pos = beat % beatsPerBar;
  const kick = sinceHit(pos, kicks, beatsPerBar);
  const snare = sinceHit(pos, snares, beatsPerBar);
  const hat = pos % 0.5;

  const low = decay(kick, 5);
  const mid = decay(snare, 6);
  const high = 0.7 * decay(hat, 9);
  const barIndex = Math.floor(beat / beatsPerBar);

  const { bands, spectrum } = spectrumFor({ low, mid, high, barIndex });

  // Notes stay listed for one beat after note-on, so a drum's decay can be drawn from `age`.
  const notes = [];
  if (kick < 1) notes.push({ note: 36, velocity: 1, channel: 1, age: kick });
  if (snare < 1) notes.push({ note: 38, velocity: 0.9, channel: 1, age: snare });
  if (hat < 0.5) notes.push({ note: 42, velocity: 0.6, channel: 1, age: hat });

  return {
    n, fps, dt: 1 / fps, t, beat, bpm, sig, width, height, seed,
    phase: beat % 1, bar: pos / beatsPerBar, beatIndex: Math.floor(pos), barIndex,
    level: clamp01(0.15 + 0.55 * low + 0.5 * mid + 0.2 * high), low, mid, high,
    onset: Math.max(decay(kick, 8), decay(snare, 8)),
    // Per-drum envelopes: 1 at the hit, about 5% half a beat later.
    kick: decay(kick, 6), snare: decay(snare, 6), hat: decay(hat, 6), age: beat,
    bands, spectrum, notes,
  };
}

export const frameAt = (n, options = {}) => frameAtTime(n / (options.fps ?? 60), { ...options, n });
