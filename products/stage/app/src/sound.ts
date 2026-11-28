// Which track's sound a take records. Stage Sound sits on the main track and carries the whole
// mix, so it comes before a Stage Link that merely has something on its sidechain. Playing tracks
// come before stopped ones either way, so a second, idle DAW is never recorded over the live one.
export type Heard = { id: string; role: number; playing: boolean; sound: boolean };
export const SOUND_ROLE = 5;

export function pickSound(tracks: Heard[]): string | null {
  const rank = (track: Heard) => Number(track.playing) * 2 + Number(track.role === SOUND_ROLE);
  return tracks.filter((track) => track.sound).sort((a, b) => rank(b) - rank(a))[0]?.id ?? null;   // ties keep the order given
}
