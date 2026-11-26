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
    kind: "MIDI articulator",
    hue: 262,
    bpm: 172,
    form: "tube",
    formLabel: "Tube",
    alt: "Cutline’s interactive phrase grammar tape, chord progression strip, gravitational piano roll, velocity lane and multi-hue expression lanes",
    body: "Sculpt deterministic Statement, Answer, Mutation, Ratchet and Turnaround phrases on a tactile piano roll with a Chord Progression Strip, bend them through a gravitational microtiming field and drive any synth with four MPE and CC lanes.",
    features: [
      [
        "Tactile Phrase Stage & Chord Strip",
        "Five BBCut-inspired grammars with Loop/Evolve modes, polyphonic breaks, Chord Progression Strip and 64-step seed history. Click spans to lock them, drag dividers, drag scale-snapped notes and durations or Alt-click to mute.",
      ],
      [
        "Live MIDI, Mono, MPE & Drums",
        "Play Live MIDI Transpose or Chord Carve with Latch, and stream Ableton-safe Mono Ch 1 (with RPN bend range), full MPE Lower Zone (Ch 2–15, ±48 st) or Drum Kit articulation with live activity LEDs.",
      ],
      [
        "8-voice PolyBLEP audition & Drag .mid",
        "Preview immediately with the anti-aliased 8-voice poly audition synth (Reese, Sub, Pluck, Kit), drag the exact current 960-PPQ .mid phrase into your DAW, and compare takes with Undo/Redo and A/B slots.",
      ],
    ],
    credits: "Includes 32 factory presets, eight macros, Undo/Redo and A/B compare.",
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
    title: "Algorithmic Breakbeat Grammar, Microtiming Gravity & MPE Articulation",
    summary:
      "Translate Nick Collins’ BBCut phrase grammars into deterministic Statement/Answer/Mutation timelines, warp beats through gravitational microtiming fields, and emit 14-bit RPN/MPE Lower Zone streams.",
    topics: [
      "BBCut Phrase Grammars",
      "Gravitational Microtiming",
      "MPE Lower Zone (±48 st)",
      "960-PPQ MIDI Export",
    ],
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
