// The page's end of the cable. Each plugin instance is one link: its latest transport block, its
// recent notes and the notes it holds. Lines arrive from the app's hub, one link per track.
import { spectrumFor } from "./take.js";

const DRUMS = { kick: [36, 35], snare: [38, 40, 37], hat: [42, 44, 46] };
export const nowMicros = () => (performance.timeOrigin + performance.now()) * 1000;

const createLink = () => ({ role: 1, block: null, hits: [], lastArrival: 0, arrangement: [] });

// One link per plugin instance, keyed by its id. Every line names its role (0 drums, 1 to 4 layers).
// `now` is when the line arrived: the wall clock live, the take's own clock on replay.
export function deliver(links, message, now = nowMicros()) {
  if (!links.has(message.id)) links.set(message.id, createLink());
  const link = links.get(message.id);
  link.role = message.role;
  receive(link, message, now);
}
export function connectHub(hub) {
  const links = new Map();
  hub.onLine((line) => deliver(links, JSON.parse(line)));
  return links;
}
// The instances heard from in the last second. A removed plugin simply stops counting.
export const alive = (links, now = nowMicros()) => [...links.values()].filter((link) => now - link.lastArrival < 1e6);

function receive(link, message, now = nowMicros()) {
  link.lastArrival = now;
  // A loop or a jump back in the DAW: forget the notes that are now in the future.
  if (link.block && message.ppq < link.block.ppq - 0.5) link.arrangement.length = 0;
  link.block = message;
  for (const [on, note, channel, velocity, ppq, us] of message.notes) {
    if (on) link.hits.push({ note, channel, velocity, ppq, us });
    // Every note with its span in beats, for the arrangement. Off stays Infinity while held.
    if (on) link.arrangement.push({ note, on: ppq, off: Infinity });
    else { const held = link.arrangement.findLast((n) => n.note === note && n.off === Infinity); if (held) held.off = ppq; }
  }
  // What is held now, for a Stage that connected mid-note or just forgot its notes after a loop.
  for (const [note, , ppq] of message.held ?? []) {
    if (!link.arrangement.some((n) => n.note === note && n.off === Infinity)) link.arrangement.push({ note, on: Math.min(ppq, message.ppq), off: Infinity });
  }
  while (link.hits.length && now - link.hits[0].us > 4e6) link.hits.shift();
  // The arrangement only ever needs the notes still held and the last few that ended.
  if (link.arrangement.length > 64) link.arrangement = link.arrangement.filter((n, i, all) => n.off === Infinity || i >= all.length - 16);
}

// Musical position now: the last block's position, carried forward by the wall clock.
// `drums` is the link whose hits and level the scenes hear. With one plugin it is the same link.
export function frameFromLink(link, { n, dt, width, height, startBeat, now = nowMicros() }, drums = link) {
  const block = link.block ?? { us: now, ppq: 0, bpm: 120, playing: false, sig: [4, 4], level: 0, macros: [] };
  const beatsPerMicro = block.bpm / 60e6;
  const beat = block.playing ? block.ppq + (now - block.us) * beatsPerMicro : block.ppq;
  const beatsPerBar = (block.sig[0] * 4) / block.sig[1];
  const position = ((beat % beatsPerBar) + beatsPerBar) % beatsPerBar;

  // One envelope per drum: 1 at the hit, about 5% half a beat later. Hits come from MIDI, never from the beat.
  const envelope = (notes) => drums.hits.reduce((peak, hit) => {
    if (!notes.includes(hit.note) || hit.us > now) return peak;
    return Math.max(peak, hit.velocity * Math.exp(-6 * (now - hit.us) * beatsPerMicro));
  }, 0);
  const kick = envelope(DRUMS.kick), snare = envelope(DRUMS.snare), hat = envelope(DRUMS.hat);
  const low = kick, mid = snare, high = 0.7 * hat;
  const barIndex = Math.floor(beat / beatsPerBar);

  return {
    n, fps: Math.round(1 / dt), dt, t: n * dt, beat, bpm: block.bpm, sig: block.sig, width, height, seed: 1,
    phase: ((beat % 1) + 1) % 1, bar: position / beatsPerBar, beatIndex: Math.floor(position), barIndex,
    age: beat - startBeat, kick, snare, hat, low, mid, high, onset: Math.max(kick, snare),
    level: Math.min(1, Math.max((drums.block?.level ?? 0) * 4, 0.15 + 0.55 * low + 0.5 * mid + 0.2 * high)),
    ...spectrumFor({ low, mid, high, barIndex }),
    notes: drums.hits.filter((hit) => hit.us <= now && (now - hit.us) * beatsPerMicro < 1)
      .map((hit) => ({ note: hit.note, velocity: hit.velocity, channel: hit.channel, age: (now - hit.us) * beatsPerMicro })),
    macros: block.macros,
  };
}

// Macro at 0.5 is the scene's own default; 0 and 1 are the parameter's minimum and maximum.
export function applyMacros(scene, macros = []) {
  (scene.manifest.params ?? []).slice(0, macros.length).forEach((param, i) => {
    const m = macros[i];
    const min = param.min ?? 0, max = param.max ?? 1;
    scene.params[param.name] = m < 0.5 ? min + (param.default - min) * m * 2 : param.default + (max - param.default) * (m - 0.5) * 2;
  });
}
