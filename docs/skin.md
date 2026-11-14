# The skin

How Livecut Redux's editor looks, and the rules that keep it that way. The
code is in `src/ui`; every value below lives in `src/ui/Theme.h` and
`Theme.cpp`.

## The idea

Livecut cuts audio, so the editor is cut paper. Everything is ink on paper
except one thing: the pad, a sheet of coloured card with a picture laid on it
in pieces of the same card. Each program has its own colour and its own
picture, so you can tell what is playing from across the room. The picture is
drawn in code, as vectors, every frame: it shows what the program does, the
pad's two axes move it the way they move the sound, and it answers every new
block of cuts.

The original Livecut (2004) was a white panel with orange controls. The paper
and Cut's orange come from it.

## Rules

- **Colour belongs to the programs.** Controls are ink and paper. The only
  colour outside the pad is a program's own: its tab, and the block being cut
  on the phrase tape.
- **A control's name is its label.** No captions under controls, no taglines,
  no helper text on screen. If something needs explaining, change the layout.
- **Tooltips are for choices, not for the pad.** Tabs and switches may explain
  themselves on hover. Nothing pops up over the art; the pad's instructions
  are help text for screen readers.
- **Status takes the tape's place.** With no phrase to show, the header says
  what to do next where the tape would be.
- **Motion is quiet, and means something.** At rest the art drifts slowly;
  while the pad plays it runs faster; each new block of cuts kicks it, and it
  settles in 0.6 s. A program change crossfades the pad (180 ms), each new
  block sends a ring out from the pointer mark (320 ms) and the About sheet
  fades (160 ms).
- **No pictures in files.** Everything on screen is drawn in code from the
  values in `Theme.h`, so it stays sharp at any size and there is nothing to
  regenerate. The only binary assets are the font and the app icon, which
  `LivecutTests "[.icon]"` draws.
- **The Classic pages keep their layout** and take the same controls.

## Colour

| Token | Value | Use |
|---|---|---|
| `paper` | `#F4F4F1` | The editor |
| `well` | `#E9E9E5` | A section set into the paper (Classic pages) |
| `rule` | ink at 14% | Hairlines, tracks, outlines |
| `ink` | `#000000` | Text, thumbs, anything chosen |
| `inkMuted` | ink at 60% | Values and secondary status |
| `accent` | `#FF6B1A` | Cut's colour, and the Classic pages' block |

| Program | Colour | Its picture | X | Y | A new block |
|---|---|---|---|---|---|
| Cut | `#FF6B1A` | Strips of tape, cut on the diagonal | More pieces | Pieces slide further out of place | Re-edits the pieces |
| Bounce | `#FA6E8C` | A ball's hops | More hops | Hops climb instead of dying away | Squashes the ball |
| Glitch | `#EC5FB3` | Tiles in rows | Smaller, more tiles | Columns stair from falling to rising | Slips other rows sideways |
| Suction | `#AB70F0` | A tunnel, drawing inward | Slides it left to right | Squeezes it narrower | Bursts it open |
| Filter | `#48A4F2` | A low-pass response, layer on layer | Moves the cutoff | Raises the resonant peak | Rings the peak |
| Tape | `#34C9C5` | Two reels and the tape between | Slacker tape | More loops of it | Brakes the reels |
| Loops | `#6CD04F` | Rings inside rings, each turning twice as fast | A smaller loop | More loops nested inside | Swells the rings |
| Rush | `#FCD21C` | Wedges racing to one point | Lifts the point, from drag to rush | Starts the fan further back | Lands on the point |

Each colour is light enough for ink to read on it, and the art is drawn in
tones of it alone. `tests/SkinTests.cpp` checks both: a 4.5:1 contrast with
ink, and that the art's average hue matches. It also checks that every
picture moves with time, with each axis and at a new block.

## Type

One family, [Bricolage Grotesque](https://github.com/ateliertriay/bricolage)
(SIL Open Font License), embedded as a single variable font. `uiFont` gives
text at 400, 500, 600 or 700; `numberFont` adds tabular figures for values
that change; `displayFont` is the wordmark's condensed 800.

Sizes: 14 for controls, 13 on the pad's tags, 16 for the About sheet's body
and the Classic pages' section titles.

## Layout

The editor is laid out once at 880 × 724 and scaled as a whole. On the pad
surface, top to bottom: the wordmark, the phrase tape and the surface switch;
the program tabs; the pad's sheet; then Hold, Straight, Beat / Bar and Mix.

The pad's sheet shows 8 points around the art on the left and top, and 24 on
the right and bottom, where a marker on each edge follows the pointer mark.
Those markers are the axis sliders, so the keyboard and screen readers reach
each axis.

## Art

`src/ui/PadArt.cpp` has one function per program. Each is a pure function of
an `ArtMotion` (a clock, the two axes, and the state of the last block), so
the same motion always draws the same picture and a test can step it. `lay`
puts a piece of card down on its own shadow.

The pad moves the motion on from the display's refresh, through a
`FrameClock`: 30 frames a second at rest, and 60 while it plays or something
on it is changing. It draws the art in software into an image and copies that
to the screen, because the platform renderers take several times longer to
draw it themselves. With both axes at their busiest a frame costs 1.8 to
4.4 ms that way at Retina size, against up to 8 ms drawn directly on macOS
(`LivecutTests "[.benchmark]" -s`).

To add a program: give it a colour in `Theme.cpp` and a scene in `PadArt.cpp`.
Its tab follows from the Pad Program parameter's choices.

## Reviewing a change

The hidden screenshot tests render the editor without a host:

```sh
LIVECUT_SCREENSHOT_DIR=shots build/LivecutTests "[.screenshot]"
```

`[.screenshot][pad]` alone renders the pad surface: stopped, at rest in each
program, mid-performance, the Classic pages and the About sheet. They are
stills; to judge the motion, load the plug-in in a host.
