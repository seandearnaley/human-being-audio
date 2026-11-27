# Cutline 3 PLAY: six programs, and a surface that draws what was sent

Spec 4 of 4. Replaces spec 1's four interim pad programs. Written 2026-11-26 while the founder was away; the
decisions here are Claude's and can be overruled.

## Programs

Six programs, all locked to the grid, all returning to the pattern in phase on release, all engaging on the
next 1/16 and releasing at once (unchanged from spec 1). `padProgram` choices become, in this order:
`Repeat, Roll, Density, Rate, Drop, Fill`. Default Repeat. Pad MIDI notes C3 to F3 engage them in that order.

| Program | X | Y |
|---------|---|---|
| Repeat | window: 1 beat, 1/2, 1/4, 1/8 beat (as now) | gate length (as now) |
| Roll | rate: 1/8, 1/16, 1/16T, 1/32 (as now) | lane: bottom third kick, middle snare, top closed hat |
| Density | Density 0-100%, replacing the parameter while held | Dynamics 0-100%, replacing the parameter while held |
| Rate | 0.5x, 1x, 2x (as now) | below 30% kick and snare only; above 70% no kick (as now) |
| Drop | four zones, left to right: silence; kick only; kick and snare; everything except kick | none |
| Fill | none: while held, every bar plays its fill (as `fillNow`) | none |

- Roll velocity rises across each bar it is held, from 72 at the bar's start to 120 at its end. Other lanes keep
  playing the pattern.
- Density: the grid and ribbon must resolve with the same Density and Dynamics the engine used, so telemetry
  reports the effective Density and Dynamics, and the UI resolves with those.
- In MIDI Slicer mode: Repeat loops the phrase's notes, Roll retriggers the held notes, Rate and Drop's silence
  zone apply, Density and Fill do nothing (their tabs are dimmed).

## The surface draws what was sent

The pad area's background is the current bar of drum notes as the engine actually sent them: eight lane rows,
16 columns, each note-on drawn as a mark at its real time and velocity, over faint outlines of the resolved
pattern. Marks fade over one bar. So whatever a program does (a Repeat loop, a roll, dropped lanes, Density
layers appearing), the surface shows it because it is what was played.

- The engine reports each drum note-on it sends (lane, beat position in the bar, velocity) through a lock-free
  single-producer telemetry channel; the UI drains it on its timer. No allocation on the audio thread.
- The XY puck, the program's zones (Repeat windows, Roll lanes, Drop zones, Density value) and the program
  colour stay as an overlay.
- The slicer mode surface draws the slicer's sent notes the same way, by pitch row.

## Dock

`Hold`, `Pad MIDI`, `XY → CC`, `Audition`, `Drag .mid`. The kit map selector moves out of PLAY (it lives in
GRID). Key hints on the tabs and in the badge show only in drum mode with Pad MIDI on.

## Bypass

`processBlockBypassed` sends a note-off for every note Cutline has sounding, then passes MIDI through, so
bypassing while a slicer or drum note sounds never leaves it hanging.

## Testing

Engine tests (`[engine][pads]`) for each new or changed program: Roll lane by Y and its velocity ramp; Density
and Dynamics override equal the same run with the parameters set; each Drop zone's lanes; Fill equals `fillNow`;
Slicer-mode behaviour. A test that the sent-note telemetry lists exactly the note-ons in the MIDI buffer. A
plugin test that bypass after a held note sends its note-off. Snapshots of PLAY for each program.
