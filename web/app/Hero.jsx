"use client";
import { useEffect } from "react";
import { PRODUCTS, SCULPTURE_FORMS } from "../lib/catalog.mjs";

const FORM_GLYPHS = {
  monoliths: (
    <>
      <rect x="2" y="5" width="3" height="9" />
      <rect x="6.5" y="2" width="3" height="12" />
      <rect x="11" y="7" width="3" height="7" />
    </>
  ),
  tube: (
    <>
      <ellipse cx="8" cy="8" rx="6.5" ry="4" fill="none" strokeWidth="2" />
      <circle cx="8" cy="8" r="1.6" />
    </>
  ),
  rings: (
    <>
      <polygon points="8,1.5 14,5 14,11 8,14.5 2,11 2,5" fill="none" strokeWidth="1.5" />
      <polygon points="8,5 11,10 5,10" fill="none" strokeWidth="1.5" />
    </>
  ),
  prisms: (
    <>
      <polygon points="8,1 14,8 8,15 2,8" />
      <line x1="2" y1="8" x2="14" y2="8" stroke="var(--paper)" strokeWidth="1" />
    </>
  ),
  helix: (
    <>
      <ellipse cx="8" cy="8" rx="6.2" ry="3.2" fill="none" strokeWidth="1.5" transform="rotate(-28 8 8)" />
      <ellipse cx="8" cy="8" rx="6.2" ry="3.2" fill="none" strokeWidth="1.5" transform="rotate(28 8 8)" />
      <circle cx="8" cy="8" r="1.5" />
    </>
  ),
  membrane: (
    <>
      <circle cx="8" cy="8" r="6.4" fill="none" strokeWidth="1.4" />
      <circle cx="8" cy="8" r="3.6" fill="none" strokeWidth="1.3" />
      <circle cx="10.2" cy="6.2" r="1.5" />
    </>
  ),
  strata: (
    <>
      <polygon points="3,4.2 13,2.8 14,5.0 2,6.4" />
      <polygon points="2,8.0 14,7.2 14,9.4 2,10.2" />
      <polygon points="3,11.6 13,11.2 12,13.4 4,13.8" />
    </>
  ),
};

export default function Hero() {
  useEffect(() => {
    let teardown = null;
    let cancelled = false;
    import("./sculpture.js").then((mod) => {
      if (cancelled) return;
      teardown = mod.mountSculpture?.() ?? null;
    });
    return () => {
      cancelled = true;
      teardown?.();
    };
  }, []);
  return (
    <section className="hero wrap" aria-labelledby="hero-title">
      <div className="hero__copy">
        <h1 className="hero__title" id="hero-title">
          <span>Free plugins</span>
          <span className="thin">by</span>
          <span>Human Being Audio.</span>
        </h1>
        <p className="hero__lede">
          Cut up a breakbeat, synthesize or resample a bassline, morph a filterbank, articulate MIDI phrases, glide chords across a pitch torus or forge phase-locked drums. Free to download,
          open to study, built for making music.
        </p>
        <div className="hero__actions">
          <a className="btn btn--primary" href="#plugins">
            Browse the plugins
          </a>
          <a className="btn" href="#install">
            How installing works
          </a>
        </div>
      </div>

      <div className="hero__grab grab" data-grab aria-hidden="true">
        <span className="grab__hint">drag to twist · tap to hit</span>
      </div>

      <aside className="hero__dock dock" aria-labelledby="dock-title" style={{ "--hue": PRODUCTS[0].hue }}>
        <div className="dock__head">
          <h2 className="dock__title" id="dock-title">
            Play with the sculpture
          </h2>
          <span className="dock__meta mono" data-backend>
            3D: loading…
          </span>
          <button type="button" className="btn btn--quiet" data-enable-scene="" hidden>Enable 3D</button>
        </div>

        <fieldset className="forms" id="form-picker">
          <legend>Form</legend>
          {SCULPTURE_FORMS.map((form, index) => (
            <label className="chip" key={form.id}>
              <input type="radio" name="form" value={form.id} defaultChecked={index === 0} />
              <svg className="glyph" viewBox="0 0 16 16" aria-hidden="true">
                {FORM_GLYPHS[form.id]}
              </svg>
              {form.label}
            </label>
          ))}
        </fieldset>

        <div className="vowel">
          <span className="vowel__axis vowel__axis--x" aria-hidden="true">
            <span>front</span>
            <span>back</span>
          </span>
          <span className="vowel__axis vowel__axis--top" aria-hidden="true">
            close
          </span>
          <span className="vowel__axis vowel__axis--bottom" aria-hidden="true">
            open
          </span>
          <div className="vowel__pad" data-vowel-pad aria-hidden="true">
            <svg
              className="vowel__art"
              viewBox="0 0 100 100"
              preserveAspectRatio="none"
            >
              <polyline className="vowel__arc" data-arc points="" />
              <path className="vowel__comb" data-comb d="" />
              <path className="vowel__fill" data-env-fill d="" />
              <path className="vowel__line" data-env-line d="" />
            </svg>

            <span className="vowel__puck"></span>
          </div>
          <input
            className="vowel__y"
            id="m-open"
            type="range"
            min="0"
            max="100"
            defaultValue="80"
            data-macro="open"
            aria-label="Vowel height, close to open"
          />
          <input
            className="vowel__x"
            id="m-formant"
            type="range"
            min="0"
            max="100"
            defaultValue="60"
            data-macro="formant"
            aria-label="Vowel position, front to back"
          />
          <p className="vowel__read">
            <output
              className="vowel__ipa"
              data-readout="vowel"
              htmlFor="m-formant m-open"
            >
              /a/
            </output>
            <span className="mono" data-readout="hz"></span>
          </p>
        </div>

        <div className="dock__pair">
          <div className="macro">
            <div className="macro__top">
              <label htmlFor="m-slice">Slice</label>
              <output htmlFor="m-slice" data-readout="slice">
                32nds
              </output>
            </div>
            <div className="ruler" aria-hidden="true"></div>
            <input
              id="m-slice"
              type="range"
              min="0"
              max="100"
              defaultValue="50"
              data-macro="slice"
            />
          </div>
          <div className="macro">
            <div className="macro__top">
              <label htmlFor="m-chaos">
                <span className="glitch">Chaos</span>
              </label>
              <output htmlFor="m-chaos" data-readout="chaos">
                15%
              </output>
            </div>
            <svg
              className="noise"
              viewBox="0 0 120 16"
              preserveAspectRatio="none"
              aria-hidden="true"
            >
              <path data-noise d="" />
            </svg>
            <input
              id="m-chaos"
              type="range"
              min="0"
              max="100"
              defaultValue="15"
              data-macro="chaos"
            />
          </div>
        </div>

        <div className="dock__row">
          <p className="dock__note">A silent, interactive sculpture.</p>
          <span>
            <button className="btn btn--quiet" type="button" data-hit>
              Hit it
            </button>
            <button
              className="btn btn--quiet"
              type="button"
              data-freeze
              aria-pressed="false"
            >
              Freeze
            </button>
          </span>
        </div>
      </aside>
    </section>
  );
}
