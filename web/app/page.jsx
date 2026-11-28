import Image from "next/image";
import Link from "next/link";
import livecutImage from "../../products/livecut-redux/press/screenshots/livecut-redux-interface.png";
import tractImage from "../../products/tract-form/press/screenshots/tract-form-interface.png";
import poleFieldImage from "../../products/pole-field/press/screenshots/pole-field-interface.png";
import cutlineImage from "../../products/cutline/press/screenshots/cutline-interface.png";
import torusGlideImage from "../../products/torus-glide/press/screenshots/torus-glide-interface.png";
import strikeBodyImage from "../../products/strike-body/press/screenshots/strike-body-interface.png";
import lineageImage from "../../products/lineage/press/screenshots/lineage-interface.png";
import datumImage from "../../products/datum/press/screenshots/datum-interface.png";
import prismVeinImage from "../../products/prism-vein/press/screenshots/prism-vein-interface.png";
import { PLATFORMS, PRODUCTS, PRODUCTS_BY_SLUG, STUDIO } from "../lib/catalog.mjs";
import { PRODUCT_MEDIA } from "../lib/media.mjs";
import { readCatalog } from "../lib/releases.mjs";
import Hero from "./Hero";
import { ProductGallery, ProductVideo } from "./ProductMedia";
import ShowcasePlayer from "./ShowcasePlayer";

export const revalidate = 60;

const PRODUCT_IMAGES = {
  "livecut-redux": livecutImage,
  "tract-form": tractImage,
  "pole-field": poleFieldImage,
  cutline: cutlineImage,
  "torus-glide": torusGlideImage,
  "strike-body": strikeBodyImage,
  lineage: lineageImage,
  datum: datumImage,
  "prism-vein": prismVeinImage,
};

function Product({ product, release, unavailable }) {
  const { slug } = product;
  const media = PRODUCT_MEDIA[slug];
  return (
    <article
      id={slug}
      className="plugin studio-product"
      data-plugin={slug}
      data-kind={product.kind}
      data-form={product.form}
      data-hue={product.hue}
      data-bpm={product.bpm}
      style={{ "--hue": product.hue }}
    >
      <div className="studio-product__body">
        <p className="mono muted">{product.kind}</p>
        <h3>{product.name}</h3>
        <p>{product.body}</p>
        {media?.video ? (
          <ProductVideo slug={slug} name={product.name} video={media.video} posterAlt={product.alt} />
        ) : (
          <Image
            src={PRODUCT_IMAGES[slug]}
            alt={product.alt}
            sizes="(max-width: 800px) 93vw, (max-width: 1152px) 92vw, 760px"
            className="studio-interface"
          />
        )}
        {media?.gallery && <ProductGallery name={product.name} items={media.gallery} />}
        <ShowcasePlayer slug={slug} productName={product.name} hue={product.hue} />
        <p className="studio-facts">{product.credits}</p>
        <p className="mono studio-facts">
          {product.license} · VST3 · Audio Unit on Mac · Standalone
        </p>
        <details>
          <summary>Inside {product.name}</summary>
          <dl>
            {product.features.map(([title, text]) => (
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
                  {PLATFORMS[asset.os]}
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
          <a href={STUDIO.repository + "/tree/main/products/" + slug}>
            Source and build instructions ↗
          </a>
          {product.learnHref && (
            <Link href={product.learnHref}>Interactive DSP Field Guide →</Link>
          )}
        </div>
      </div>
    </article>
  );
}

export default async function Page() {
  let releases = [];
  let errors = new Map();
  let unavailable = false;
  try {
    ({ releases, errors } = await readCatalog());
    for (const error of errors.values()) console.error(error);
  } catch (error) {
    console.error(error);
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
                  <a href={STUDIO.repository + "/releases"}>
                    See releases on GitHub.
                  </a>
                </p>
              )}
              <div className="catalog__grid">
                <div className="catalog__list">
                  {PRODUCTS.map((product) => (
                    <Product
                      key={product.slug}
                      product={product}
                      release={releases.find((r) => r.product === product.slug)}
                      unavailable={unavailable || errors.has(product.slug)}
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
                    <b data-window-name="">{PRODUCTS[0].name}</b>
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
                    href={STUDIO.repository + "/releases.atom"}
                  >
                    Atom feed
                  </a>
                </div>
                {releases.length ? (
                  <ul className="notes__list">
                    {releases.map((r) => (
                      <li key={r.product}>
                        <a href={r.url}>
                          {PRODUCTS_BY_SLUG[r.product].name} {r.version}
                        </a>{" "}
                        · <time dateTime={r.date}>{r.date}</time>
                      </li>
                    ))}
                  </ul>
                ) : (
                  <p>
                    {unavailable || errors.size > 0
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
                the Audio Unit (Audio Unit Music Effect for Livecut Redux, Pole
                Field and Prism Vein, Audio Unit MIDI FX for Cutline) and VST3
                in the system Audio Plug-Ins folder and the standalone in
                Applications. All nine plugins require macOS 12 or later and
                contain universal Apple Silicon and Intel binaries.
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
