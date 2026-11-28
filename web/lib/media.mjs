// Product videos and screenshot galleries, keyed by catalog slug. Files live in
// web/public/media/<slug>/; tests/media.test.mjs checks every path, size and budget.
// Full views are 2560 px wide; `detail: true` marks a 1:1 Retina crop of part of the interface.

export const PRODUCT_MEDIA = {
  "livecut-redux": {
    video: {
      title: "Livecut Redux in Ableton Live 12",
      badge: "Watch it cut",
      seconds: 46,
      width: 1920,
      height: 1080,
      poster: "/media/livecut-redux/livecut-redux-demo-poster.webp",
      // WebM first: Chromium builds without H.264 take it, Safari and the rest fall through to MP4.
      sources: [
        { src: "/media/livecut-redux/livecut-redux-demo.webm", type: 'video/webm; codecs="vp9, opus"' },
        { src: "/media/livecut-redux/livecut-redux-demo.mp4", type: 'video/mp4; codecs="avc1.640029, mp4a.40.2"' },
      ],
      caption:
        "Recorded live in Ableton Live 12 at 172 BPM: arrangement automation plays the pad through all eight programs, then the Classic surface runs Recut and Impact. The break comes from the studio’s own synthesized showcase drum kit.",
    },
    gallery: [
      {
        src: "/media/livecut-redux/gallery/01-live-session-pad.webp",
        width: 2560,
        height: 1440,
        label: "In Live · Pad",
        alt: "Livecut Redux in Ableton Live 12, cutting a drum break while the arrangement’s Pad Program, Pad X, Pad Y and Pad Touch automation lanes play it, with the Bounce program on screen",
      },
      {
        src: "/media/livecut-redux/gallery/02-pad-cut.webp",
        width: 2560,
        height: 1438,
        label: "Pad · Cut",
        alt: "The pad playing Cut, with the phrase tape above it marking the block now playing and the Re-edit and Density readouts in its corners",
      },
      {
        src: "/media/livecut-redux/gallery/03-pad-tape.webp",
        width: 2560,
        height: 1438,
        label: "Pad · Tape",
        alt: "The Tape program, drawn as two reels and a loop of tape, braking the break to a halt on the grid",
      },
      {
        src: "/media/livecut-redux/gallery/04-pad-rush.webp",
        width: 2560,
        height: 1438,
        label: "Pad · Rush",
        alt: "The Rush program, drawn as gold rays converging on the next downbeat as it rolls the last beats of the bar",
      },
      {
        src: "/media/livecut-redux/gallery/05-pad-loops.webp",
        width: 2560,
        height: 1438,
        label: "Pad · Loops",
        alt: "The Loops program, drawn as nested green rings folding into shorter loops of the first hit",
      },
      {
        src: "/media/livecut-redux/gallery/06-classic-recut.webp",
        width: 2560,
        height: 1438,
        label: "Classic · Recut",
        alt: "The Classic surface on CutProc11 in Studio mode, with Phrase and Cut controls, Recut and Attack Align counting the cuts it has aligned, and the crusher and comb filter",
      },
      {
        src: "/media/livecut-redux/gallery/07-classic-impact.webp",
        width: 2560,
        height: 1438,
        label: "Classic · Impact",
        alt: "The Impact tab: a shared cut envelope driving the Slice Filter, Drive in Wavefold mode and the Stereo Transient Shaper",
      },
      {
        src: "/media/livecut-redux/gallery/08-live-session-impact.webp",
        width: 2560,
        height: 1440,
        label: "In Live · Impact",
        alt: "Impact automated in Ableton Live 12: Filter Cutoff, Drive Gain and Transient Attack lanes over the break while Livecut Redux shows its Impact tab",
      },
    ],
  },
  "tract-form": {
    video: {
      title: "Tract Form in Ableton Live 12",
      badge: "Watch it roll",
      seconds: 39,
      width: 1920,
      height: 1080,
      poster: "/media/tract-form/tract-form-demo-poster.webp",
      sources: [
        { src: "/media/tract-form/tract-form-demo.webm", type: 'video/webm; codecs="vp9, opus"' },
        { src: "/media/tract-form/tract-form-demo.mp4", type: 'video/mp4; codecs="avc1.640029, mp4a.40.2"' },
      ],
      caption:
        "Recorded live in Ableton Live 12 at 172 BPM: a MIDI clip plays the bassline while arrangement automation moves the macros, Morph and Drive, across three factory presets (Dark Techstep Roller, Throat Wobble and Speaking Pipe). The drums come from the studio’s own synthesized showcase drum kit.",
    },
    gallery: [
      {
        src: "/media/tract-form/gallery/01-live-session-roller.webp",
        width: 2560,
        height: 1440,
        label: "In Live · Roller",
        alt: "Tract Form in Ableton Live 12: a MIDI bassline clip with Tract, Warp, Coupling and Width automation lanes above a drum loop, with Dark Techstep Roller open",
      },
      {
        src: "/media/tract-form/gallery/02-dark-techstep-roller.webp",
        width: 2560,
        height: 1522,
        label: "Dark Techstep Roller",
        alt: "Dark Techstep Roller: the master sine and sub, Reese A and B, a Pre tract from æ to u, Buchla fold, Comb sculpt, mid/side and output, with scope, goniometer and spectrum below",
      },
      {
        src: "/media/tract-form/gallery/03-throat-wobble.webp",
        width: 2560,
        height: 1522,
        label: "Throat Wobble",
        alt: "Throat Wobble: a Post tract from i to æ after a Fold wavefolder, with the Morph lane moving the vowels in time",
      },
      {
        src: "/media/tract-form/gallery/04-speaking-pipe.webp",
        width: 2560,
        height: 1522,
        label: "Speaking Pipe",
        alt: "Speaking Pipe: a Post tract from a to u over a Sine fold and Bell sculpt, its sub held mono beneath the growl",
      },
      {
        src: "/media/tract-form/gallery/05-oscillator-flow.webp",
        detail: true,
        width: 1920,
        height: 1080,
        label: "Oscillator flow",
        alt: "The oscillator flow up close: the master sine feeding Reese A and B and their feedback loop into the tract, with the sub lock line running beneath",
      },
      {
        src: "/media/tract-form/gallery/06-movement-lanes.webp",
        detail: true,
        width: 1920,
        height: 1080,
        label: "Movement lanes",
        alt: "The movement lanes up close: each lane’s target, rate, sync mode, phase and depth, with the Morph lane running",
      },
      {
        src: "/media/tract-form/gallery/07-macros-and-output.webp",
        detail: true,
        width: 1920,
        height: 1080,
        label: "Macros & output",
        alt: "The eight macros, Coupling, Beat, Reach, Tract, Warp, Sculpt, Motion and Width, above the fold, sculpt, mid/side and output stages",
      },
      {
        src: "/media/tract-form/gallery/08-live-session-pipe.webp",
        width: 2560,
        height: 1440,
        label: "In Live · Pipe",
        alt: "Speaking Pipe in Ableton Live 12, with Morph, Drive and Coupling automation lanes moving the vowels and the growl over a drum loop",
      },
    ],
  },
};
