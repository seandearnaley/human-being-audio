import Image from "next/image";
import studio from "../../studio.json";
import livecutImage from "../../products/livecut-redux/docs/editor.png";
import tractImage from "../../products/tract-form/press/screenshots/tract-form-interface.png";
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
    alt: "Tract Form’s oscillator flow, formant controls, live analysis and four movement lanes",
    body: "One master sine feeds two reese voices and a clean mono sub. Morph between vowels, fold the growl and draw movement across four tempo-locked lanes.",
    features: [
      [
        "Sound engine",
        "Two phase-modulated reese voices share one pitch reference with the clean sub. Use Beat lock to align their movement to a note division.",
      ],
      [
        "Movement",
        "Four tempo-locked lanes can draw a custom modulation curve or follow another lane at a chosen ratio.",
      ],
      [
        "Resample",
        "Render four bars offline with 16× oversampling and drag the WAV into a session.",
      ],
    ],
    credits: "Includes 28 factory presets and eight performance macros.",
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
                the Audio Unit and VST3 in the system Audio Plug-Ins folder and
                the standalone in Applications. Livecut Redux requires macOS 11
                or later. Tract Form requires macOS 12 or later. Both packages
                contain Apple Silicon and Intel binaries.
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
