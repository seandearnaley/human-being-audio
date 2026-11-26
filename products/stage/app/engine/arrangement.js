// The arrangement rule for one layer: a note is a scene, and a note that starts while the last one
// is still held starts a Cut from the old scene to the new one. The Cut's length is declared, not
// read from the overlap, because a plugin cannot see when a held note will end.
// A pure function of the notes heard so far and the beat, so it runs live and replays exactly.
//   notes: [{ note, on, off }] in beats; off is Infinity while the note is held.
export function layerAt(notes, beat, cutBeats) {
  const heard = notes.filter((n) => n.on <= beat).sort((a, b) => a.on - b.on);
  const latest = heard.at(-1);
  if (!latest) return null;
  const previous = heard.at(-2);
  const progress = (beat - latest.on) / cutBeats;
  if (previous && previous.off > latest.on && progress < 1) return { from: previous, to: latest, progress };
  const held = heard.filter((n) => n.off > beat).at(-1);
  return held ? { from: null, to: held, progress: 1 } : null;
}
