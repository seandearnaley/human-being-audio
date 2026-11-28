// The plugins' lines, as the app's page needs them: forwarded untouched to the output frame, and
// boiled down a few times a second to what the top bar, the drums row and the knob strip show.
import { useEffect, useRef, useState } from "react";
import { pickSound } from "./sound";

type Track = { role: number; bpm: number; ppq: number; playing: boolean; sig: [number, number]; macros: number[]; sound: boolean; us: number };
export type Transport = {
  connected: boolean; playing: boolean; bpm: number; position: string;
  knobs: Record<number, number[]>;                       // by role: 0 drums, 1 to 4 layers
  hit: { kick: boolean; snare: boolean; hat: boolean };
  soundId: string | null;                                // the track whose sound a take would record
};
const IDLE: Transport = { connected: false, playing: false, bpm: 0, position: "", knobs: {}, hit: { kick: false, snare: false, hat: false }, soundId: null };
const DRUMS: Record<number, "kick" | "snare" | "hat"> = { 35: "kick", 36: "kick", 37: "snare", 38: "snare", 40: "snare", 42: "hat", 44: "hat", 46: "hat" };
const nowMicros = () => (performance.timeOrigin + performance.now()) * 1000;

export function useHub(forward: (line: string) => void): Transport {
  const [transport, setTransport] = useState(IDLE);
  const send = useRef(forward);
  send.current = forward;

  useEffect(() => {
    const tracks = new Map<string, Track>();
    const lastHit = { kick: 0, snare: 0, hat: 0 };
    const stop = window.stage.onLine((line) => {
      send.current(line);
      const message = JSON.parse(line);
      tracks.set(message.id, { role: message.role, bpm: message.bpm, ppq: message.ppq, playing: message.playing, sig: message.sig, macros: message.macros, sound: message.frame !== undefined, us: message.us });
      if (message.role === 0) for (const [on, note] of message.notes) if (on && DRUMS[note]) lastHit[DRUMS[note]] = message.us;
    });
    let last = "";
    const timer = setInterval(() => {
      const now = nowMicros();
      for (const [id, track] of tracks) if (now - track.us > 1e6) tracks.delete(id);
      const heard = [...tracks].sort(([, a], [, b]) => Number(b.playing) - Number(a.playing));
      const clock = heard[0]?.[1];
      let next = IDLE;
      if (clock) {
        const beat = clock.playing ? clock.ppq + ((now - clock.us) * clock.bpm) / 60e6 : clock.ppq;
        const perBar = (clock.sig[0] * 4) / clock.sig[1];
        const inBar = ((beat % perBar) + perBar) % perBar;
        next = {
          connected: true, playing: clock.playing, bpm: Math.round(clock.bpm),
          position: `${Math.floor(beat / perBar) + 1} . ${Math.floor(inBar) + 1} . ${Math.floor((inBar % 1) * 4) + 1}`,
          knobs: Object.fromEntries(heard.map(([, track]) => [track.role, track.macros.map((value) => Math.round(value * 100) / 100)])),
          hit: { kick: now - lastHit.kick < 12e4, snare: now - lastHit.snare < 12e4, hat: now - lastHit.hat < 8e4 },
          soundId: pickSound(heard.map(([id, track]) => ({ id, ...track }))),
        };
      }
      const key = JSON.stringify(next);
      if (key !== last) { last = key; setTransport(next); }
    }, 33);
    return () => { stop(); clearInterval(timer); };
  }, []);
  return transport;
}
