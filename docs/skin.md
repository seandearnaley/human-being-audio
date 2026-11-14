# The skin

How Livecut Redux's editor looks, and the rules that keep it that way. The
code is in `src/ui`; every value below lives in `src/ui/Theme.h` and
`Theme.cpp`.

## The idea

Livecut cuts audio, so the pad is cut card. The editor is black with white
type and controls, and the only colour on it is a program's: the pad is a
sheet of the program's colour, with a picture laid on it in pieces of card in
tones of that colour. Each program has its own colour and its own picture, so
you can tell what is playing from across the room. The picture is drawn in
code, as vectors, every frame: it shows what the program does, the pad's two
axes move it the way they move the sound, and it keeps time with the host.

The original Livecut (2004) was a white panel with orange controls. Cut's
orange comes from it.

## Rules

- **Colour belongs to the programs.** Controls are white on black. The only
  colour outside the pad is a program's own: its tab, and the block being cut
  on the phrase tape.
- **A control's name is its label.** No captions under controls, no taglines,
  no helper text on the pad's surface. If something needs explaining, change
  the layout.
- **Tooltips are for choices, not for the pad.** Tabs and switches may explain
  themselves on hover. Nothing pops up over the art; the pad's instructions
  are help text for screen readers.
- **Status takes the tape's place.** With no phrase to show, the editor says
  what to do next where the tape would be. On the Classic pages a control's
  status sits beside that control.
- **Motion keeps time.** While the pad cuts, its art follows the host's beat,
  the pointer mark swells on each beat, and a tab that is waiting for its
  switch lights on each beat. Each new block of cuts kicks the art, which
  settles in 0.6 s, sends a ring out from the pointer mark (320 ms), and
  drops CUT in the wordmark under its blade, to spring back (300 ms). At rest
  the art keeps a slow pulse of its own, a beat every two seconds. A program
  change crossfades the pad (180 ms) and the About sheet fades (160 ms).
- **No pictures in files.** Everything on screen is drawn in code from the
  values in `Theme.h`, so it stays sharp at any size and there is nothing to
  regenerate. The only binary assets are the font and the app icon, which
  `LivecutTests "[.icon]"` draws.
- **One grid.** Both surfaces are laid out on it; see Layout.

## Colour

| Token | Value | Use |
|---|---|---|
| `backdrop` | `#000000` | The editor |
| `well` | `#161616` | A card on the Classic pages, and a tab at rest |
| `rule` | white at 18% | Hairlines, tracks, outlines |
| `ink` | `#FFFFFF` | Text, thumbs, anything chosen |
| `inkMuted` | white at 60% | Values and status |
| `onColour` | `#000000` | Text and marks on a program's colour |
| `accent` | `#FF6B1A` | Cut's colour, and the Classic pages' block |

| Program | Colour | Its picture | X | Y | On the beat | A new block |
|---|---|---|---|---|---|---|
| Cut | `#FF6B1A` | Strips of tape, cut on the diagonal | More pieces | Pieces slide further out of place | Rows shunt against each other | Re-edits the pieces |
| Bounce | `#FA6E8C` | A ball's hops | More hops | Hops climb instead of dying away | One hop to a beat | Swells the landings |
| Glitch | `#EC5FB3` | Tiles in rows | Smaller, more tiles | Columns stair from falling to rising | Tiles light on the sixteenths | Slips other rows sideways |
| Suction | `#AB70F0` | A tunnel, drawing inward | Slides it left to right | Squeezes it narrower | A ring every two beats | Bursts it open |
| Filter | `#48A4F2` | A low-pass response, layer on layer | Moves the cutoff | Raises the resonant peak | The peak swells | Rings the peak |
| Tape | `#34C9C5` | Two reels and the tape between | Slacker tape | More loops of it | Reels turn a third of the way round | Brakes the reels |
| Loops | `#6CD04F` | Rings inside rings, each turning twice as fast | A smaller loop | More loops nested inside | The outer ring turns once a bar | Swells the rings |
| Rush | `#FCD21C` | Wedges racing to one point | Lifts the point, from drag to rush | Starts the fan further back | A dart down each seam once a bar | Lands on the point |

Each colour is light enough for black to read on it, and for its tab's name
to read on a resting tab. The art's bright tones are the colour itself; its
deep tones lean toward red or blue on their way to black, so the warm colours
never pass through brown. `tests/SkinTests.cpp` checks the contrast (4.5:1
both ways), that the art's bright card matches its colour, and that every
picture moves with the beat, with each axis and at a new block.

## Type

One family, [Bricolage Grotesque](https://github.com/ateliertriay/bricolage)
(SIL Open Font License), embedded as a single variable font. `uiFont` gives
text at 400, 500, 600 or 700; `numberFont` adds tabular figures for values
that change; `displayFont` is the wordmark's condensed 800.

Sizes: 14 for controls and card titles, 13 on the pad's tags, 12 for status
and advice, and 16 for the About sheet's body. Where lines of different sizes
are stacked, each is set from its first letter's own edge (`sideBearing`), so
they share one.

## Layout

The editor is laid out once at 880 × 724 and scaled as a whole. It is a stack
of bands, 20 points apart and 20 from every edge. The header is the same on
both surfaces: the wordmark at the left, the surface switch at the right, and
everything in it on one centre line.

**The pad surface.** The tape runs along the header, centred between the
wordmark and the switch, which are the same width. Below are the program
tabs, the pad's sheet, then Hold, Straight, Beat / Bar and Mix on one centre
line. The sheet shows the same 20 points around the art on every side, with a
marker on each side that follows the pointer mark. The bottom and right sides
are the axis sliders, so the keyboard and screen readers reach each axis.

**The Classic surface.** The header holds a tab for each page, each as wide
as its name, then the procedures. Below it are the tape, a page of cards, and
a strip of controls that every page shares. Cards sit in halves or thirds, 12
apart, with 12 of padding at their sides and 8 above and below their rows. A
row is 28 high (a title, a switch, a drop-down or a button) or 40 (a slider
under its caption and value), and rows are 4 apart. A slider's thumb travels
from its caption's edge to its value's edge.

`tests/SkinTests.cpp` holds both surfaces to the grid.

## Art

`src/ui/PadArt.cpp` has one function per program. Each is a pure function of
an `ArtMotion` (a count of beats, the two axes, and the state of the last
block), so the same motion always draws the same picture and a test can step
it. `lay` puts a piece of card down on its own shadow.

While the pad cuts, the count of beats is the host's: the pad measures the
tempo from the phrase position and corrects its phase a little each frame, so
the picture stays on the beat without jumping. The pad moves the motion on
from the display's refresh, through a `FrameClock`: 30 frames a second at
rest, and 60 while it plays or something on it is changing. It draws the art
in software into an image and copies that to the screen, because the platform
renderers take longer to draw it themselves. With both axes at their busiest
a frame costs 1.7 to 4.5 ms of processor time that way at Retina size,
against up to 9 ms drawn directly on macOS (`LivecutTests "[.benchmark]" -s`).

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
