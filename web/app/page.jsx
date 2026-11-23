import Image from "next/image";
import studio from "../../studio.json";
import livecutImage from "../../products/livecut-redux/docs/editor.png";
import tractImage from "../../products/tract-form/press/screenshots/tract-form-interface.png";
import poleFieldImage from "../../products/pole-field/press/screenshots/pole-field-interface.png";
import cutlineImage from "../../products/cutline/press/screenshots/cutline-interface.png";
import { readCatalog } from "../lib/releases.mjs";
import Hero from "./Hero";

export const revalidate = 60;
const products = {
  "livecut-redux": {
    kind: "Beat slicer",
    hue: 43,
    bpm: 168,
    form: "monoliths",
    image: livecutImage,
    alt: "Livecut Redux’s orange performance pad, eight program tabs and phrase ruler",
    body: "Cut a break into phrases of repeats that follow your host’s tempo. Play eight programs from the pad, or open Classic to shape the original BBCut procedures.",
    features: [
      [
        "Pad and Classic",
        "Move across the pad to play two settings of a program. Switch to Classic for direct access to the four cutting procedures.",
      ],
      [
        "Host transport",
        "Cuts follow the host tempo and transport. At rest, the dry signal passes through the output trim.",
      ],
      ["Capture", "Save the last complete phrase as a stereo WAV."],
    ],
    credits:
      "A modern port of Remy Muller’s Livecut, based on Nick Collins’ BBCut.",
  },
  "tract-form": {
    kind: "Bass synthesizer",
    hue: 194,
    bpm: 172,
    form: "prisms",
    image: tractImage,
    alt: "Tract Form’s oscillator flow, formant controls, live analysis, modulation arcs and four movement lanes",
    body: "One master sine feeds two reese voices and a clean mono sub. Morph between vowels, fold the growl, animate four tempo-locked lanes and play live with full MIDI expression.",
    features: [
      [
        "Sound engine",
        "Two phase-modulated reese voices share one pitch reference with the clean sub. Use Beat lock to align their movement to a note division.",
      ],
      [
        "Movement & MIDI expression",
        "Four tempo-locked curve lanes plus smooth CC1, CC74, aftertouch and RPN pitch-bend modulation, visualized with live modulation ghost arcs across macros and bars.",
      ],
      [
        "Resample & A/B workflow",
        "Render four bars offline with 16× oversampling and drag the WAV into a session, with 64-step Undo/Redo and A/B compare in the top bar.",
      ],
    ],
    credits: "Includes 30 factory presets, eight macros, Undo/Redo and A/B compare.",
  },
  "pole-field": {
    kind: "Morphing filterbank",
    hue: 318,
    bpm: 172,
    form: "rings",
    image: poleFieldImage,
    alt: "Pole Field’s 2D Z-plane unit circle, 14-pole magnitude curve, stereo goniometer, modulation arcs and four movement lanes",
    body: "Morph fourteen poles and fourteen zeros across 3D filter cubes with polar interpolation, inter-stage ADAA2 drive, mono Sub Lock, Burg LPC audio imprinting and live MIDI expression.",
    features: [
      [
        "14-pole Z-plane lattice",
        "Seven cascaded TPT pole-zero sections in Serial, Hybrid or Parallel topology across eight curated morph cubes, with MIDI pitch tracking and CC1/CC74/aftertouch morphing.",
      ],
      [
        "Inter-stage ADAA2 drive",
        "Place Tube, Clip, Fold, Buchla or Sine saturation before, between or after filter stages with matched 2×/4×/8× HIIR oversampling and clean mono Sub Lock.",
      ],
      [
        "Audio imprint, movement & A/B",
        "Stamp any WAV’s 14th-order Burg LPC pole structure into the Imprint Cube, watch live modulation arcs across six macros, and compare edits with 64-step Undo/Redo and A/B slots.",
      ],
    ],
    credits: "Includes 12 factory presets, eight macros, Undo/Redo and A/B compare.",
  },
  cutline: {
    kind: "MIDI articulator",
    hue: 262,
    bpm: 172,
    form: "tube",
    image: cutlineImage,
    alt: "Cutline’s interactive phrase grammar tape, gravitational piano roll, velocity lane, eight macros and multi-hue expression lanes",
    body: "Sculpt deterministic Statement, Answer, Mutation, Ratchet and Turnaround phrases directly on the piano roll, bend them through a gravitational microtiming field and drive any synth with four tempo-locked MPE and CC lanes.",
    features: [
      [
        "Direct-manipulation Phrase Stage",
        "Five BBCut-inspired grammars with Loop/Evolve modes, six melodic contours and 64-step seed history. Click spans to lock them, drag dividers, drag notes snapped to scale or Alt-click to mute.",
      ],
      [
        "Live MIDI, Mono, MPE & Drums",
        "Play Live MIDI Transpose or Chord Carve with Latch, and stream Ableton-safe Mono Ch 1 (with RPN bend range), full MPE Lower Zone (Ch 2–15, ±48 st) or Drum Kit articulation with live activity LEDs.",
      ],
      [
        "Poly audition, Drag .mid & A/B",
        "Preview immediately with the 4-voice poly audition synth (Reese, Sub, Pluck, Kit), drag the exact current 960-PPQ .mid phrase into your DAW, and compare takes with Undo/Redo and A/B slots.",
      ],
    ],
    credits: "Includes 24 factory presets, eight macros, Undo/Redo and A/B compare.",
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
          sizes="(max-width: 800px) 93vw, 550px"
          className="studio-interface"
        />
        <figure className="studio-audio">
          <figcaption>
            {slug === "livecut-redux"
              ? "Hear Rush on a drum loop"
              : slug === "pole-field"
                ? "Hear Neuro Chew on a bassline"
                : slug === "cutline"
                  ? "Hear Neuro Whip at 172 BPM"
                  : "Hear Tract Form on MIDI note 29"}
          </figcaption>
          <audio
            controls
            preload="none"
            aria-label={product.name + " audio example"}
            src={"/audio/" + slug + ".flac"}
          />
        </figure>
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
        <a href={studio.repository + "/tree/main/products/" + slug}>
          Source and build instructions ↗
        </a>
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
    <>
      <div className="stage-scope">
        <div className="stage" id="stage" aria-hidden="true">
          <div className="stage__light" />
          <div className="fallback-orb" />
          <canvas id="stage-canvas" />
        </div>
        <main id="main">
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
        </main>
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
                the Audio Unit (Audio Unit MIDI FX for Cutline) and VST3 in the
                system Audio Plug-Ins folder and the standalone in Applications.
                Livecut Redux requires macOS 11 or later. Tract Form, Pole Field
                and Cutline require macOS 12 or later. Packages contain Apple
                Silicon and Intel binaries.
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
    </>
  );
}
