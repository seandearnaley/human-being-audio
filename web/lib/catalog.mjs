import studio from "../../studio.json" with { type: "json" };

export const STUDIO = studio;

export const PLATFORMS = {
  macos: "macOS",
  windows: "Windows x64",
  linux: "Ubuntu 26.04 x86_64",
};

const PRODUCT_DETAILS = {
  "livecut-redux": {
    kind: "Beat slicer",
    hue: 43,
    bpm: 168,
    form: "monoliths",
    formLabel: "Monoliths",
    alt: "Livecut Redux 2.0’s GPU performance pad, eight MIDI-playable program tabs, live phrase waveform tape and output peak meter",
    body: "Cut a break into tempo-locked phrases of repeats with causal cubic resampling and mono Sub-Anchor. Play eight GPU-rendered programs from the pad or over MIDI C1–G1, or open Classic for deep BBCut, Fracture and Tuned Resonator control.",
    features: [
      [
        "Pad, MIDI & Classic",
        "Move across the pad or trigger all eight programs live via MIDI C1–G1 with CC1/CC74/aftertouch expression. Switch to Classic for direct access to CutProc11, WarpCut, SQPusher, Bounce, GrainCut, TapeCut and Tuned Resonator.",
      ],
      [
        "Sub-Anchor & safety limiter",
        "Keep sub-bass mono and rock-solid beneath breakbeat chops with a global LR4 Sub-Anchor crossover (40–200 Hz) and zero-latency C² linked safety limiter.",
      ],
      [
        "One-drag WAV & A/B workflow",
        "Drag the last complete phrase straight into your DAW as a 32-bit float stereo WAV, backed by 64-step Undo/Redo, A/B compare and Roll Seed.",
      ],
    ],
    credits:
      "Includes 28 factory presets, Undo/Redo and A/B compare. A modern Visage port of Remy Muller’s Livecut, based on Nick Collins’ BBCut.",
  },
  "tract-form": {
    kind: "Bass synthesizer",
    hue: 194,
    bpm: 172,
    form: "prisms",
    formLabel: "Prisms",
    alt: "Tract Form’s oscillator flow, Pre/Post formant tract, live analysis, modulation arcs and four movement lanes",
    body: "One master sine feeds two phase-modulated reese voices and a clean LR8-locked mono sub. Morph vowels Pre or Post ADAA2 wavefolding, shape ADSR & Sub Punch, carve Stereo Sculpt notches and animate four tempo-locked lanes.",
    features: [
      [
        "Sub-locked neuro bass engine",
        "Phase-aligned LR8 crossover with Dome Hilbert phase rotation, continuous Sub Warmth, ADSR + Sub Punch envelope, and 2×/4×/8× oversampled ADAA2 shaping (Fold, Sine, Tube, Buchla, Clip).",
      ],
      [
        "Pre/Post vocal tract & Stereo Sculpt",
        "Morph six vowels before or after wavefolding, carve multi-notch Bell, Phaser or Comb Stereo Sculpt, and drive everything via four Sync/Retrig/1-Shot lanes plus smooth CC1, CC74, aftertouch and RPN pitch bend.",
      ],
      [
        "Resample & A/B workflow",
        "Render four bars offline with 16× oversampling and drag the WAV into a session, with zero-latency C² safety limiting, 64-step Undo/Redo and A/B compare.",
      ],
    ],
    credits: "Includes 42 factory presets, eight macros, Undo/Redo and A/B compare.",
  },
  "pole-field": {
    kind: "Morphing filterbank",
    hue: 318,
    bpm: 172,
    form: "rings",
    formLabel: "Rings",
    learnHref: "/learn/pole-field",
    alt: "Pole Field’s 2D Z-plane unit circle, 14-pole magnitude curve, stereo goniometer, modulation arcs and four movement lanes",
    body: "Morph fourteen poles and fourteen zeros across twelve 3D filter cubes with polar interpolation, audio-rate FM, envelope following, inter-stage ADAA2 drive, mono Sub Lock and Burg LPC audio imprinting.",
    features: [
      [
        "14-pole Z-plane lattice",
        "Seven cascaded TPT pole-zero sections in Serial, Hybrid or Parallel topology across twelve curated morph cubes, with Stereo/Mid/Side modes, audio-rate FM, MIDI pitch tracking and CC1/CC74/aftertouch morphing.",
      ],
      [
        "Inter-stage ADAA2 drive & Sub Lock",
        "Place Tube, Clip, Fold, Buchla or Sine saturation before, between or after filter stages with matched 2×/4×/8× HIIR oversampling, mono LR4 Sub Lock and zero-latency safety limiting.",
      ],
      [
        "Audio imprint, Env lanes & A/B",
        "Stamp any WAV’s 14th-order Burg LPC pole structure into the Imprint Cube, modulate from internal Input/Sidechain envelope followers, and compare edits with 64-step Undo/Redo and A/B slots.",
      ],
    ],
    credits: "Includes 42 factory presets, eight macros, Undo/Redo and A/B compare.",
  },
  cutline: {
    kind: "Drum sequencer & MIDI slicer",
    hue: 262,
    bpm: 172,
    form: "tube",
    formLabel: "Tube",
    alt: "Cutline’s eight-lane drum grid with four scene selectors, density and swing controls, and its GRID and PLAY tabs",
    body: "Write a beat across eight lanes, switch between four scenes and use the PLAY pad to repeat, roll or drop parts without losing your place. MIDI Slicer plays held chords and captured clips through a gate pattern.",
    features: [
      ["Layered drum grid", "Draw Core, Groove and Detail hits. Density reveals the layers, swing delays offbeat sixteenths and Dynamics changes their velocity contrast."],
      ["Six PLAY programs", "Hold Repeat, Roll, Density, Rate, Drop or Fill. Each engages on the next sixteenth and returns to the pattern in phase when you let go."],
      ["MIDI Slicer", "Gate held notes or a captured clip across up to 32 steps. Use the built-in audition sound or send MIDI to another instrument."],
    ],
    credits: "Includes 46 patterns, 13 fills and 22 factory presets.",
  },
  "torus-glide": {
    kind: "Pitch-manifold synth",
    hue: 198,
    bpm: 172,
    form: "helix",
    formLabel: "Helix",
    alt: "Torus Glide’s interactive 5-voice Möbius ribbon canvas, Scale Gravity rails, live chord capture pills, sound and stereo space core, and two tempo-locked movement lanes",
    body: "Unify bar-locked Shepard-Risset bass spirals, programmable non-pop polyphonic chord bends and hybrid Möbius endless chord glides in one engine. Lock a mono Sub-Anchor beneath 7-octave spirals, bend five voices smoothly through Scale and Just-Intonation Gravity Wells, or stream live MPE.",
    features: [
      [
        "Spiral, Copedent & Möbius modes",
        "Glide 7-octave bar-locked Shepard-Risset bass and chord spirals with constant-RMS Hann² normalization, program 4-scene dark voicings with Tymoczko Auto-Lead, Pedal-Steel stagger and Scale/JI Gravity Wells, or fuse both into Möbius infinite chord glides.",
      ],
      [
        "Neuro Notch, Sub-Anchor & Stereo Space",
        "Five timbres (Reese Saw, Pure Sine, Formant Choir, Wavefold FM, Pulse Sub) feeding an LR4 mono Sub-Anchor with harmonic warmth, HIIR oversampled ADAA2 shaping, 4-mode Cytomic SVF (LP 12, LP 24, Bandpass, Neuro Notch) and upper-band Stereo Space.",
      ],
      [
        "Live chord capture, MPE & Drag exports",
        "Hold any MIDI chord and click Cap A–D to capture voicings live, switch scenes via C0–D#0 keyswitches, stream MPE Lower Zone (±48 st), or drag 8× bar-locked .wav loops and 960-PPQ .mid clips straight into your DAW.",
      ],
    ],
    credits: "Includes 28 factory presets, two drawable movement lanes, Undo/Redo and A/B compare.",
  },
  "strike-body": {
    kind: "Percussion synth",
    hue: 24,
    bpm: 172,
    form: "membrane",
    formLabel: "Membrane",
    alt: "Strike Body’s 40 ms Transient Microscope, glowing interactive Chladni modal drumhead, six performance macro dials, Quick-Start Archetypes, Mutate pad, and four-column percussion rack",
    body: "Synthesize phase-locked neuro snares, kicks, claps, metallic anvils and chromatic toms from three coupled layers: a pitch-whip Fundamental Body, a 24-mode nonlinear tensioned Modal Shell, and transient-ducked Wire & Click.",
    features: [
      [
        "40 ms Microscope & Grid-Locked Auto-Align",
        "Inspect latency-compensated Body, Shell, Wire and Sum waveforms live over the first 40 ms. Drag across the scope to scrub alignment and start phase, or hit Auto-Align to maximize constructive transient summation at 0.00 ms grid drift.",
      ],
      [
        "24-Mode Nonlinear Shell & 6 Mallet Exciters",
        "Tap or drag the Chladni drumhead across six physical geometries (Snare Shell, Kirchhoff Plate, Drumhead, Metallic Bar, Hollow Pipe, Glass Bell) and six mallets (Wood Stick, Hard Felt, Metal Hammer, Rim Flam, Skin Thump, Laser Snap).",
      ],
      [
        "16-Stage Series Disperser, Macros & Drag .wav",
        "Shape transients with a flat-magnitude 16-stage series allpass Disperser, Tone Scoop, stereo Studio Room, HIIR oversampled ADAA2 clipping, six automatable macros, Smart Mutate and sample-0 key-tagged one-shot .wav drag.",
      ],
    ],
    credits: "Includes 24 factory presets, six Quick-Start Archetypes, Mutate, Undo/Redo and A/B compare.",
  },
  lineage: {
    kind: "Resampling bass synth",
    hue: 166,
    bpm: 172,
    form: "strata",
    formLabel: "Strata",
    alt: "Lineage’s glowing geological strata pad, non-destructive Print/Stack Gen-2 resampler ribbon, PLAY/STUDIO view switch, and unified studio Voice footer bar",
    body: "Sculpt analog, sync and TZFM Reese basses, then print and stack them non-destructively into a live Strata resampler without leaving the synth. Lock a phase-pure mono sub beneath 4-voice polyphonic chords, crossfade-scrub reversed or pitched Gen-2 layers across an LR4 crossover, or drop in your own audio.",
    features: [
      [
        "Non-destructive Gen-2 Strata resampler",
        "Print 1–4 bar loops in-plugin, stack up to 4 generations with instant undo, scrub loop windows with equal-power crossfades, split Blend / Hi-Strata / Lo-Strata at 460 Hz, or drag-and-drop external WAV/AIFF/FLAC stems.",
      ],
      [
        "Mono, Legato & Poly 4 with lowest-note sub lock",
        "Run six zero-aliasing Reese/Sync/TZFM cores through dual morphing filters, 4× ADAA1 waveshapers, phase dispersion and harmonic density while a dedicated LR4 mono sub tracks the lowest active chord note.",
      ],
      [
        "Sculptural PLAY & STUDIO workflow",
        "Perform across six expressive XY programs in PLAY or dial every filter rail, modulation lane and Voice bar control in STUDIO, backed by 64-step Undo/Redo, A/B compare and instant drag-out WAV export.",
      ],
    ],
    credits: "Includes 24 factory presets, four modulation lanes, Undo/Redo and A/B compare.",
  },
  datum: {
    kind: "Neuro kick synth",
    hue: 152,
    bpm: 172,
    form: "piston",
    formLabel: "Piston",
    alt: "Datum’s phase-separated Sub/Body/Click/Sum waveform stage with direct-manipulation Pitch and Amp splines, six luminous macro dials, Quick-Start Archetypes, and unified Voice footer bar",
    body: "Synthesize phase-locked neurofunk, Twin-T, and modal membrane kicks from pure DSP before any FX chain. Sculpt analytical pitch and amplitude splines with Zero-X tail snap, balance Sub/Body/Click without crossover phase smear, align directly against a live sidechain sub-bass with Sub-Link, and compose 4-slot A/B/C/D grooves.",
    features: [
      [
        "Four Nord-philosophy pure-DSP cores & analytical splines",
        "Switch between analytical Sine, nonlinear discrete Twin-T bridged-T, 6-partial Harmonic with Schroeder/Peak-Min crest optimization, and Berger tension Membrane + Helmholtz Cavity — driven by Gauss-Legendre phase-integrated splines and terminal Zero-X snap.",
      ],
      [
        "Phase-native 3-band sculpting & Sub-Link alignment",
        "Balance Sub, Body, and Click without crossover smear, saturate through 8× HIIR ADAA2 shapers with analytical DC cancellation and zero-phase Box-Cut, rotate phase with an Orchard allpass rotator, or one-click Match Phase and Tune Root against a live sidechain bass bus.",
      ],
      [
        "4-slot Kit Composer, Ref Sub & Sample-0 Drag exports",
        "Build Main, Ghost, Double and Sub kit variations across a 16/32-step velocity and ratchet sequencer with a built-in Reference Sub-Bass, or drag 16× sample-0 key-tagged .wav bounces and 960-PPQ .mid grooves straight into your DAW.",
      ],
    ],
    credits: "Includes 32 factory presets, 4-slot Kit Composer, Sub-Link sidechain alignment, Undo/Redo and A/B compare.",
  },
  "prism-vein": {
    kind: "Spectral colorizer",
    hue: 212,
    bpm: 172,
    form: "lattice",
    formLabel: "Lattice",
    alt: "Prism Vein’s 60 FPS Spectral Prism Lattice with interactive 12-note chord strip, upward harmonic bloom and downward Bark Auto-Tame overlay, COLOR/LATTICE switcher, four movement lanes, and Voice footer bar",
    body: "Turn raw neuro reeses, drum breaks and growls into phase-locked harmonic glass. Drive four real-time spectral engines—Chord Lattice, Carrier Morph, Harmonic Comb and Glass Freeze—from MIDI chords, an interactive chord strip or a live stereo sidechain while Bark Auto-Tame melts harshness and a mono LR4 Sub Shield protects the low end.",
    features: [
      [
        "Four engines & Zero-Latency Live mode",
        "Switch between 4× COLA Chord Lattice, stereo sidechain Carrier Morph, 12-voice stereo Harmonic Comb and shimmering Glass Freeze—or flip to Live 0 ms for zero-latency tracking with real-time FFT visualization.",
      ],
      [
        "Bark Auto-Tame, Sub Shield & Transient Preserve",
        "Keep club-ready weight intact with a phase-matched mono LR4 Sub Shield, dual-RMS Transient Preserve that lets drum cracks bypass smearing, and a two-pass 24-band Bark Auto-Tame suppressor that cuts harsh resonance spikes without dulling sheen.",
      ],
      [
        "MIDI chords, Freeze, 4 movement lanes & A/B",
        "Play polyphonic chords live with Sustain, Pitch Bend and Mod Wheel expression, paint voicings on the 12-note chord strip, capture spectral Freeze halos, and animate parameters across four tempo-locked curve lanes with 64-step Undo/Redo and A/B slots.",
      ],
    ],
    credits:
      "Includes 28 factory presets, built-in Neuro/Break audition loop, Undo/Redo and A/B compare.",
  },
  ligature: {
    kind: "MIDI chord & bridge generator",
    hue: 295,
    bpm: 122,
    form: "knot",
    formLabel: "Knot",
    alt: "Ligature’s song strip of a Verse and Chorus joined by a backdoor bridge, harmony lane with Roman numerals, function words and tension line, Melody, Chords, Arp and Bass part lanes over a stutter row, chord-neighbourhood ring inspector, and keyboard lens",
    body: "Write up to four sections, each with its own key, length and chord progression, joined in a ring by short bridges that modulate between their keys. Ligature generates chords, bass, melody and arp parts, then lets you launch sections and fire bridges like clips, locked to your DAW’s transport and timed backwards so the next section lands on the grid.",
    features: [
      [
        "Eight bridge routes & a theory lens",
        "Modulate through common-tone, pivot-chord, backdoor, ii-V, Neapolitan, diminished-pivot, hexatonic-pole or stutter-slam bridges, and read every chord by its Roman numeral, a Home/Away/Pull/Colour function word, the tension line and a neighbourhood ring of in-key, borrowed and outside substitutes.",
      ],
      [
        "Four generated parts with one-click mutation",
        "Shape chords (Hold, Stabs, 2-Step, Push, Pulse), bass (Anchor, Garage, Offbeat, Rolling), a melody motif and an arp (Up 16ths, Pluck 8ths, Counter) by density, register and rate, then Nudge, Twist or Rewrite them, pin notes and add 1/32, 1/64 or gate stutter to any step.",
      ],
      [
        "Live launching, followers & Drag MIDI",
        "Launch sections onto a Bar, 2-bar or Phrase grid (a late launch squeezes the bridge to fit) or Hold a bridge’s first bar to build, play the section key’s own chords on white keys C4–B5, give each part its own track with follower instances, and drag a section, the whole song or your last performance out as MIDI.",
      ],
    ],
    credits:
      "Includes 24 factory songs from pop and garage to DnB and ambient, a Studio audition sound built on Torus Glide, Lineage, Strike Body and Datum, Undo/Redo and A/B compare.",
  },
};

export const PRODUCT_SLUGS = Object.keys(studio.products);

export const PRODUCTS = PRODUCT_SLUGS.map((slug) => ({
  slug,
  ...studio.products[slug],
  ...PRODUCT_DETAILS[slug],
}));

export const PRODUCTS_BY_SLUG = Object.fromEntries(PRODUCTS.map((p) => [p.slug, p]));

export const SCULPTURE_FORMS = [
  { id: "monoliths", label: "Monoliths" },
  { id: "tube", label: "Tube" },
  { id: "rings", label: "Rings" },
  { id: "prisms", label: "Prisms" },
  { id: "helix", label: "Helix" },
  { id: "membrane", label: "Membrane" },
  { id: "strata", label: "Strata" },
  { id: "piston", label: "Piston" },
  { id: "lattice", label: "Lattice" },
  { id: "knot", label: "Knot" },
];

export const FIELD_GUIDE_CHAPTERS = [
  {
    num: "Chapter 01",
    slug: "pole-field",
    href: "/learn/pole-field",
    status: "Live Interactive Guide",
    live: true,
    hue: PRODUCTS_BY_SLUG["pole-field"].hue,
    title: "Painting With Poles: 14-Pole Z-Plane Morphing & Burg LPC Imprinting",
    summary:
      "Drag poles and zeros on a live unit circle, hear why naive biquad morphing explodes while log-polar interpolation stays silky, A/B inter-stage ADAA2 saturation, and imprint resonances from any sound using 14th-order Burg LPC.",
    topics: [
      "Unit Circle & Bilinear Z-Plane",
      "Cytomic TPT SVF + OTA Damping",
      "Log-Polar vs Cartesian Stability",
      "ADAA2 Anti-Aliasing",
      "14th-Order Burg LPC",
    ],
  },
  {
    num: "Chapter 02",
    slug: "tract-form",
    href: null,
    status: "Coming Next",
    live: false,
    hue: PRODUCTS_BY_SLUG["tract-form"].hue,
    title: "Why Sub-Bass Falls Apart: LR8 Crossovers, Hilbert Phase Rotation & Wavefolding",
    summary:
      "Hear phase cancellation tear a neuro reese apart, lock the sub with a phase-aligned 8th-order Linkwitz-Riley split + Dome Hilbert rotator, and inspect 5-cell Buchla 259 ADAA2 antiderivatives.",
    topics: [
      "LR8 Mono Sub-Lock",
      "Dome Hilbert Phase Rotation",
      "Pre/Post Formant Tracts",
      "HIIR Polyphase Oversampling",
    ],
  },
  {
    num: "Chapter 03",
    slug: "torus-glide",
    href: null,
    status: "In Production",
    live: false,
    hue: PRODUCTS_BY_SLUG["torus-glide"].hue,
    title: "Endless Pitch & Dark Harmony: Bar-Locked Shepard Spirals & Voice-Led Bends",
    summary:
      "Build phase-coherent 7-octave Shepard-Risset bass risers locked to host PPQ, normalize loudness with constant-RMS Hann² bells, and solve Tymoczko minimal-distance voice leading across Scale Gravity Wells.",
    topics: [
      "Unwrapped PPQ Phase Torus",
      "Constant-RMS Hann² Bells",
      "Tymoczko Voice Leading",
      "Scale & JI Gravity Wells",
    ],
  },
  {
    num: "Chapter 04",
    slug: "cutline",
    href: null,
    status: "Planned",
    live: false,
    hue: PRODUCTS_BY_SLUG.cutline.hue,
    title: "Drum grids, performance fills and MIDI slicing",
    summary:
      "Follow a beat from its three density layers to MIDI output, then hold a PLAY gesture and hear how the sequencer returns to the groove in phase.",
    topics: ["Layered patterns", "Four scenes", "Quantized PLAY gestures", "MIDI Slicer"],
  },
  {
    num: "Chapter 05",
    slug: "livecut-redux",
    href: null,
    status: "Planned",
    live: false,
    hue: PRODUCTS_BY_SLUG["livecut-redux"].hue,
    title: "Causal Cubic Resampling & Zero-Click Breakbeat Slicing",
    summary:
      "Deconstruct how Livecut Redux (GPL-3.0 free forever) captures rolling audio rings, performs causal cubic tape braking and pitch-climbing ratchets, and guards output peaks at zero latency.",
    topics: [
      "Causal Cubic Resampling",
      "Zero-Latency C² Limiter",
      "Ring Buffer Capture",
      "GPL-3.0 Heritage",
    ],
  },
  {
    num: "Chapter 06",
    slug: "strike-body",
    href: null,
    status: "Planned",
    live: false,
    hue: PRODUCTS_BY_SLUG["strike-body"].hue,
    title: "Phase-Locked Drums: 24-Mode Nonlinear Shells & Series Allpass Dispersion",
    summary:
      "See why layered drum samples lose punch to sub-millisecond phase cancellation, excite 24 Bessel & Kirchhoff modal oscillators with six physical mallets, and build a 16-stage flat-magnitude allpass Disperser.",
    topics: [
      "40 ms Transient Phase Alignment",
      "24-Mode Nonlinear Tensioned Shells",
      "16-Stage Series Allpass Disperser",
      "Sympathetic Wire Buzz & Hadamard Room",
    ],
  },
  {
    num: "Chapter 07",
    slug: "lineage",
    href: null,
    status: "Planned",
    live: false,
    hue: PRODUCTS_BY_SLUG.lineage.hue,
    title: "Non-Destructive Resampling: Lock-Free Strata Printing & Lowest-Note Sub Lock",
    summary:
      "Replace manual DAW bounce-and-reimport loops with lock-free triple-buffered generation stacking, equal-power wrap crossfading, LR4 band-split Strata crossovers, and polyphonic Reese voicings anchored to the lowest active chord note.",
    topics: [
      "Lock-Free Triple-Buffered Print Ring",
      "Equal-Power Loop Crossfade & Scrub",
      "LR4 Strata Band-Split Crossover",
      "Poly 4 Lowest-Note Mono Sub Lock",
    ],
  },
  {
    num: "Chapter 08",
    slug: "datum",
    href: null,
    status: "Planned",
    live: false,
    hue: PRODUCTS_BY_SLUG.datum.hue,
    title: "Phase-Locked Kick Synthesis: Analytical Splines, Schroeder Crest & Sub-Link",
    summary:
      "Integrate kick pitch splines analytically with Gauss-Legendre quadrature, lower harmonic crest factor with Schroeder & Peak-Min phase solvers, eliminate DC offset in asymmetric shapers, and phase-lock kicks against a live sidechain sub-bass.",
    topics: [
      "Gauss-Legendre Analytical Pitch Phase",
      "Schroeder & Peak-Min Crest Solver",
      "Analytic DC-Cancelled ADAA2 Shaping",
      "Sub-Link Live Sidechain Phase Alignment",
    ],
  },
  {
    num: "Chapter 09",
    slug: "prism-vein",
    href: null,
    status: "Planned",
    live: false,
    hue: PRODUCTS_BY_SLUG["prism-vein"].hue,
    title: "Spectral Color Bass: 4× COLA Prism Vocoding, Bark Auto-Tame & Sub Shielding",
    summary:
      "Build a zero-allocation 2-for-1 stereo STFT prism engine with exact Hann² COLA overlap-add, protect drum transients via unity-sum dual-RMS separation, lock mono sub-bass with an LR4 Sub Shield, and tame resonance spikes across 24 Bark bands.",
    topics: [
      "2-for-1 Stereo STFT (4× Hann² COLA)",
      "24-Band Bark Auto-Tame",
      "Unity-Sum Transient Shield",
      "Just-Intonation Harmonic Lattice",
    ],
  },
  {
    num: "Chapter 10",
    slug: "ligature",
    href: null,
    status: "Planned",
    live: false,
    hue: PRODUCTS_BY_SLUG.ligature.hue,
    title: "Voice-Led Modulation: Eight Bridge Routes & Backtimed Launch Scheduling",
    summary:
      "Voice each chord nearest to the one before so the voices barely move across a key change, compare eight modulation routes from common-tone pedals and pivot chords to Neapolitan, diminished and hexatonic-pole bridges, and schedule every bridge backwards from its landing so a late launch still lands on the phrase.",
    topics: [
      "Minimal-Motion Voice-Leading",
      "Eight Modulation Route Families",
      "Pivot Chords & Hexatonic Poles",
      "Backtimed, Squeezable Bridge Scheduling",
    ],
  },
  {
    num: "Track 0",
    slug: "foundations",
    href: null,
    status: "Planned",
    live: false,
    hue: 156,
    title: "Shipping Real-Time Safe, Signed 3-OS C++20 Audio Plugins",
    summary:
      "The production craft almost nobody writes down: enforcing [[clang::nonblocking]] & RealtimeSanitizer, lock-free StateMailbox<T>, Visage GPU SDF shaders, pluginval level 10, and notarized 3-OS GitHub CI.",
    topics: [
      "[[clang::nonblocking]] + RTSan",
      "Lock-Free StateMailbox",
      "Visage GPU SDF UI",
      "Signed 3-OS Release CI",
    ],
  },
];
