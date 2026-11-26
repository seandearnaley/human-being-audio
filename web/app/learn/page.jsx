import Link from "next/link";
import { FIELD_GUIDE_CHAPTERS, PRODUCTS_BY_SLUG, STUDIO } from "../../lib/catalog.mjs";
import "./learn.css";

export const metadata = {
  title: "DSP Field Guides · Human Being Audio",
  description:
    "Interactive, two-layer VST & audio DSP masterclasses — play live Web Audio engines in your browser on Layer 1, then trace the real-time C++20 math and source code on Layer 2.",
  alternates: { canonical: "/learn" },
};

export default function LearnHubPage() {
  return (
    <main id="main" className="fg-main">
      <section className="fg-hero" style={{ "--hue": PRODUCTS_BY_SLUG["pole-field"].hue }}>
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
          <h2 id="curriculum-heading">Studio Field Guide Curriculum</h2>
        </div>

        <div className="fg-grid">
          {FIELD_GUIDE_CHAPTERS.map((ch) => (
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
                        ? `${STUDIO.repository}/tree/main/modules`
                        : `${STUDIO.repository}/tree/main/products/${ch.slug}`
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
