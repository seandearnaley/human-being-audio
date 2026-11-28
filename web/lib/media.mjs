// Product videos and screenshot galleries, keyed by catalog slug. Files live in
// web/public/media/<slug>/; tests/media.test.mjs checks every path, size and budget.

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
        alt: "The Classic surface on CutProc11 in Studio mode, with Phrase and Cut controls, Recut and Attack Align, and the crusher and comb filter",
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
};
