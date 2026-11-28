# Ligature

Ligature is a chord, phrase and bridge MIDI plugin for producers who write on a loop. Write each section while it loops, then launch the next: Ligature plays a voice-led bridge between them, key changes included, timed so the new section lands on the phrase. By [Human Being Audio](https://humanbeingaudio.com).

## The window

- **Song strip**: up to four sections (A to D) in a ring, with a bridge between each neighbour. Each section has its own key, length (1 to 16 bars) and chords.
- **Harmony lane**: every chord with its name, Roman numeral and a plain word for what it does (home, away, pull, colour), plus the tension line. A bridge shows its chords in both keys and tags the pivot chord.
- **Part lanes**: a piano roll for Melody, Chords, Arp and Bass. The chords lane draws how each voice moves to the next; a held common tone is gold.
- **Stutter lane**: 1/32 and 1/64 ratchets and gates on any 16th, acting on every part.
- **Inspector**: the chord's neighbourhood in three rings (in key, borrowed, outside), voicing, inversion and colour; a part's style, density, register, rate, mutation and other takes; a bridge's route and length; a section's key, length and the progression library.
- **Keyboard and lens**: the sounding notes by part, and one sentence about the chord under the playhead.

## Live

- **Land**: Next bar, 2 bars or Phrase (4 bars). A launched section arrives on that grid.
- **Launch a section** and its bridge starts by itself, timed backwards from the landing. Launch late and the bridge is regenerated shorter so every part stays on the grid. Shift-launch skips the bridge.
- **Tap a bridge** to fire it under the same rule. **Hold** it and its first bar repeats, building, until you let go; then it plays its cadence bar and lands.
- **Scene** parameter: automating it to A-D launches that section while the host plays.
- **MIDI notes C-1 to B-1** perform: C-1 to D#-1 launch sections A-D, E-1 to G-1 tap bridges 1-4, G#-1 to B-1 hold them.
- **One-key chords**: white keys C4 to B5 (MIDI 60-83, middle C = C4) play the playing section's own chords (bass on channel 2, chord on channel 1).
- **Played chords**: hold three or more notes elsewhere on the keyboard and Ligature names the chord, ready to drop into the selected slot.

## Bridges and key changes

Eight route families, ordered from smooth to bold: common tone, pivot chord (when one exists), backdoor, ii-V, Neapolitan, diminished pivot, hexatonic pole and stutter slam. Each route is written in eight beats and plays at 1, 2 or 4 bars.

## In the DAW

Ligature follows the host's position every block, so loop braces, scrubbing and tempo changes keep it on the host's grid (a jump releases held notes and lands any bridge in flight). While the host is stopped, its own preview clock plays (the top bar reads PREVIEW instead of HOST).

- **Leader**: all parts on MIDI channels 1-4 (chords, bass, melody, arp).
- **Followers**: set Instance to Follow chords, bass, melody or arp. A follower plays the leader's song and outputs one part on channel 1, so each part can sit on its own track. Use this in Ableton Live, where a track cannot filter another track's MIDI by channel.
- In Logic, the Audio Unit is a MIDI FX.
- **Drag MIDI**: the focused section or bridge, the whole song, or the last performance (everything played since Play, bridges included), one track per part.

Ligature plays on its own, so you hear a song before you route it anywhere. **Sound source** chooses the built-in sound: Studio (the default) plays chords and melody through Torus Glide, bass through Lineage, arp and claps through Strike Body and the kick through Datum; Sketch is a lighter synth. Turn **Sound** off when your own instruments take the MIDI.

## Files

Songs are JSON with `"format": 2`. A Ligature 1 session opens on the default song.

## For developers

- `engine` preset: core, engine and their tests, no JUCE or Visage. `dev` preset: everything.
- `lg-render` renders a song to `.wav` (through the engine, with launches and holds) or `.mid`. `lg-ui-snapshot` renders the editor to a PNG.
- Design: `research/ligature-2-design.html` at the repository root (Dossier 08).

```sh
cmake --preset engine && cmake --build --preset engine && ctest --preset engine
```

## License

`AGPL-3.0-or-later`. See [LICENSE](LICENSE).
