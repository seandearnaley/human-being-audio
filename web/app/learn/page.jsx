import Link from "next/link";
import studio from "../../../studio.json";
import "./learn.css";

export const metadata = {
  title: "DSP Field Guides · Human Being Audio",
  description:
    "Interactive, two-layer VST & audio DSP masterclasses — play live Web Audio engines in your browser on Layer 1, then trace the real-time C++20 math and source code on Layer 2.",
  alternates: { canonical: "/learn" },
};

const chapters = [
  {
    num: "Chapter 01",
    slug: "pole-field",
    href: "/learn/pole-field",
    status: "Live Interactive Guide",
    live: true,
    hue: 318,
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
    hue: 194,
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
    hue: 198,
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
    hue: 262,
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
    hue: 43,
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
    hue: 24,
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

export default function LearnHubPage() {
  return (
    <main id="main" className="fg-main">
      <section className="fg-hero" style={{ "--hue": 318 }}>
        <div className="wrap fg-hero__grid">
          <div>
            <p className="fg-eyebrow">Human Being Audio · Open Copyleft Curriculum</p>
            <h1 className="fg-title">DSP Field Guides.</h1>
            <p className="fg-lede">
              First-class interactive manuals and VST engineering masterclasses — built straight
              from the real shipping C++20 engines inside Human Being Audio. Play with live Web
              Audio sandboxes in plain English on Layer 1, then peel back the math, Z-plane plots,
              and annotated source code on Layer 2.
            </p>
          </div>
          <aside className="fg-manifesto" aria-label="Copyleft philosophy">
            <p className="fg-eyebrow" style={{ color: "oklch(0.80 0.14 156)" }}>
              Embracing Copyleft &amp; Open Craft
            </p>
            <p>
              <strong>Every plugin’s DSP, UI, and Field Guide is open source.</strong> No black-box
              “analog magic” buzzwords, no DRM, no locked textbooks. Read the algorithms, experiment
              in your browser, or clone and compile everything yourself from GitHub.
            </p>
            <p>
              When you just want signed, notarized macOS Universal, Windows, and Linux installers
              and curated presets ready for Ableton — the packaged studio releases exist for
              effortless convenience and to fund the next instrument.
            </p>
          </aside>
        </div>
      </section>

      <section className="wrap" aria-labelledby="curriculum-heading">
        <div style={{ marginBlockStart: "var(--space-6)" }}>
          <p className="fg-eyebrow">Curriculum</p>
          <h2 id="curriculum-heading">Seven Interactive Chapters</h2>
        </div>

        <div className="fg-grid">
          {chapters.map((ch) => (
            <article
              key={ch.slug}
              className={`fg-card${ch.live ? " fg-card--live" : ""}`}
              style={{ "--hue": ch.hue }}
            >
              <div className="fg-card__head">
                <span className="mono muted">{ch.num}</span>
                <span className={`fg-badge${ch.live ? " fg-badge--live" : ""}`}>
                  {ch.status}
                </span>
              </div>
              <h3>{ch.title}</h3>
              <p className="muted" style={{ fontSize: "var(--step--1)" }}>
                {ch.summary}
              </p>
              <ul className="fg-card__topics" role="list">
                {ch.topics.map((topic) => (
                  <li key={topic}>{topic}</li>
                ))}
              </ul>
              <div style={{ marginBlockStart: "var(--space-2)" }}>
                {ch.href ? (
                  <Link className="btn" href={ch.href}>
                    Open Interactive Field Guide →
                  </Link>
                ) : (
                  <a
                    className="mono"
                    style={{ fontSize: "var(--step--2)", color: "var(--ink-2)" }}
                    href={
                      ch.slug === "foundations"
                        ? `${studio.repository}/tree/main/modules`
                        : `${studio.repository}/tree/main/products/${ch.slug}`
                    }
                  >
                    Browse shipping C++ source on GitHub ↗
                  </a>
                )}
              </div>
            </article>
          ))}
        </div>
      </section>
    </main>
  );
}
