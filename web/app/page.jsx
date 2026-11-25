import Image from "next/image";
import Link from "next/link";
import studio from "../../studio.json";
import livecutImage from "../../products/livecut-redux/docs/editor.png";
import tractImage from "../../products/tract-form/press/screenshots/tract-form-interface.png";
import poleFieldImage from "../../products/pole-field/press/screenshots/pole-field-interface.png";
import cutlineImage from "../../products/cutline/press/screenshots/cutline-interface.png";
import torusGlideImage from "../../products/torus-glide/press/screenshots/torus-glide-interface.png";
import strikeBodyImage from "../../products/strike-body/press/screenshots/strike-body-interface.png";
import { readCatalog } from "../lib/releases.mjs";
import Hero from "./Hero";
import ShowcasePlayer from "./ShowcasePlayer";

export const revalidate = 60;
const products = {
  "livecut-redux": {
    kind: "Beat slicer",
    hue: 43,
    bpm: 168,
    form: "monoliths",
    image: livecutImage,
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
    image: tractImage,
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
    image: poleFieldImage,
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
    image: cutlineImage,
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
    image: torusGlideImage,
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
    image: strikeBodyImage,
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
};
const platforms = {
  macos: "macOS",
  windows: "Windows x64",
  linux: "Ubuntu 26.04 x86_64",
};
function Product({ slug, release, unavailable }) {
  const product = studio.products[slug],
    detail = products[slug];
  return (
    <article
      id={slug}
      className="plugin studio-product"
      data-plugin={slug}
      data-kind={detail.kind}
      data-form={detail.form}
      data-hue={detail.hue}
      data-bpm={detail.bpm}
      style={{ "--hue": detail.hue }}
    >
      <div className="studio-product__body">
        <p className="mono muted">{detail.kind}</p>
        <h3>{product.name}</h3>
        <p>{detail.body}</p>
        <Image
          src={detail.image}
          alt={detail.alt}
          sizes="(max-width: 800px) 93vw, (max-width: 1152px) 92vw, 760px"
          className="studio-interface"
        />
        <ShowcasePlayer slug={slug} productName={product.name} hue={detail.hue} />
        <p className="studio-facts">{detail.credits}</p>
        <p className="mono studio-facts">
          {product.license} · VST3 · Audio Unit on Mac · Standalone
        </p>
        <details>
          <summary>Inside {product.name}</summary>
          <dl>
            {detail.features.map(([title, text]) => (
              <div key={title}>
                <dt>
                  <strong>{title}</strong>
                </dt>
                <dd>{text}</dd>
              </div>
            ))}
          </dl>
        </details>
        {release ? (
          <>
            <h4>
              Download {product.name} {release.version}
            </h4>
            <div className="studio-actions">
              {release.downloads.map((asset) => (
                <a key={asset.os} className="btn" href={asset.url}>
                  {platforms[asset.os]}
                </a>
              ))}
            </div>
            <details>
              <summary>Files and checksums</summary>
              <dl>
                {release.downloads.map((asset) => (
                  <div key={asset.os}>
                    <dt>
                      {asset.file} · {(asset.bytes / 1e6).toFixed(1)} MB
                    </dt>
                    <dd>
                      <code>{asset.sha256}</code>
                    </dd>
                  </div>
                ))}
              </dl>
            </details>
          </>
        ) : (
          <p>
            {unavailable
              ? "Download status is temporarily unavailable."
              : "A packaged studio release has yet to be published."}
          </p>
        )}
        <div className="studio-actions" style={{ gap: "0.75rem 1.25rem", alignItems: "center" }}>
          <a href={studio.repository + "/tree/main/products/" + slug}>
            Source and build instructions ↗
          </a>
          {slug === "pole-field" && (
            <Link href="/learn/pole-field">Interactive DSP Field Guide →</Link>
          )}
        </div>
      </div>
    </article>
  );
}
export default async function Page() {
  let releases,
    unavailable = false;
  try {
    releases = await readCatalog();
  } catch (error) {
    console.error(error);
    releases = [];
    unavailable = true;
  }
  return (
    <main id="main">
      <div className="stage-scope">
        <div className="stage" id="stage" aria-hidden="true">
          <div className="stage__light" />
          <div className="fallback-orb" />
          <canvas id="stage-canvas" />
        </div>
        <div className="stage-content">
          <Hero />
          <section
            className="section catalog"
            id="plugins"
            aria-labelledby="plugins-title"
          >
            <div className="wrap">
              <div className="section__head">
                <h2 id="plugins-title">The plugins</h2>
              </div>
              {unavailable && (
                <p role="status">
                  GitHub release data is unavailable.{" "}
                  <a href={studio.repository + "/releases"}>
                    See releases on GitHub.
                  </a>
                </p>
              )}
              <div className="catalog__grid">
                <div className="catalog__list">
                  {Object.keys(studio.products).map((slug) => (
                    <Product
                      key={slug}
                      slug={slug}
                      release={releases.find((r) => r.product === slug)}
                      unavailable={unavailable}
                    />
                  ))}
                </div>
                <div
                  className="stage-window grab"
                  data-grab=""
                  data-anchor="catalog"
                  aria-hidden="true"
                >
                  <p className="window-label">
                    <b data-window-name="">Livecut Redux</b>
                    <span className="mono muted" data-window-kind="" />
                  </p>
                </div>
              </div>
            </div>
          </section>
          <section className="section notes" id="notes">
            <div className="wrap notes__grid">
              <div>
                <div className="section__head">
                  <h2>Release notes</h2>
                  <a
                    className="btn"
                    href={studio.repository + "/releases.atom"}
                  >
                    Atom feed
                  </a>
                </div>
                {releases.length ? (
                  <ul className="notes__list">
                    {releases.map((r) => (
                      <li key={r.product}>
                        <a href={r.url}>
                          {studio.products[r.product].name} {r.version}
                        </a>{" "}
                        · <time dateTime={r.date}>{r.date}</time>
                      </li>
                    ))}
                  </ul>
                ) : (
                  <p>
                    {unavailable
                      ? "Check GitHub for release notes."
                      : "The first studio releases will appear here when published."}
                  </p>
                )}
              </div>
              <div
                className="stage-window"
                data-anchor="notes"
                aria-hidden="true"
              />
            </div>
          </section>
        </div>
      </div>
      <section className="section install" id="install">
        <div className="wrap">
          <div className="section__head">
            <h2>Install notes</h2>
          </div>
          <div className="faq">
            <details>
              <summary>macOS</summary>
              <div className="faq__body">
                Close your host and open the signed, notarized PKG. It installs
                the Audio Unit (Audio Unit Music Effect for Livecut Redux and
                Pole Field, Audio Unit MIDI FX for Cutline) and VST3 in the
                system Audio Plug-Ins folder and the standalone in Applications.
                All six plugins require macOS 12 or later and contain universal
                Apple Silicon and Intel binaries.
                <p>
                  If Tract Form still shows the old publisher, quit your host.
                  In <code>~/Library/Audio/Plug-Ins</code>, check VST3 and Components for
                  older Tract Form bundles. Move those copies to a backup folder
                  outside the plug-in folders, then reopen your host and rescan.
                </p>
              </div>
            </details>
            <details>
              <summary>Windows</summary>
              <div className="faq__body">
                Close your host and run the signed x64 installer. It puts the
                VST3 in the system VST3 folder and adds the standalone to the
                Human Being Audio Start menu folder. Use Installed apps to
                uninstall.
              </div>
            </details>
            <details>
              <summary>Ubuntu</summary>
              <div className="faq__body">
                On Ubuntu 26.04 x86_64, extract the archive and copy the entire
                .vst3 directory into <code>~/.vst3/</code>. Rescan in your host.
                The standalone runs from the extracted Standalone folder.
              </div>
            </details>
            <details>
              <summary>Verify a download</summary>
              <div className="faq__body">
                Compare its SHA-256 with the value under Files and checksums.
                The GitHub release also includes SHA256SUMS and build
                provenance. Check provenance with{" "}
                <code>
                  gh attestation verify FILE --repo
                  seandearnaley/human-being-audio
                </code>
                .
              </div>
            </details>
          </div>
        </div>
      </section>
    </main>
  );
}
