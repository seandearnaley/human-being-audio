"use client";

import { useState } from "react";
import Link from "next/link";
import { REPO } from "./components/GuidePrimitives";
import Lab1UnitCircle from "./components/Lab1UnitCircle";
import Lab2MorphStability from "./components/Lab2MorphStability";
import Lab3MorphCube from "./components/Lab3MorphCube";
import Lab4AdaaDrive from "./components/Lab4AdaaDrive";
import Lab5BurgImprint from "./components/Lab5BurgImprint";
import { usePoleFieldAudio } from "./components/usePoleFieldAudio";

export default function PoleFieldGuide() {
  const [depth, setDepth] = useState("both"); // "musician" | "both" | "engineer"
  const { activeAudioLab, audioStatus, toggleLabAudio, stopAllAudio, postAudioState } =
    usePoleFieldAudio();

  const showMusician = depth === "musician" || depth === "both";
  const showEngineer = depth === "engineer" || depth === "both";
  const sharedLabProps = {
    depth,
    showMusician,
    showEngineer,
    activeAudioLab,
    toggleLabAudio,
    postAudioState,
  };

  return (
    <main id="main" className="fg-main" style={{ "--hue": 318 }}>
      {/* Hero */}
      <section className="fg-hero">
        <div className="wrap fg-hero__grid">
          <div>
            <p className="fg-eyebrow">
              <Link href="/learn">DSP Field Guides</Link> · Chapter 01 · Pole Field
            </p>
            <h1 className="fg-title">Painting With Poles.</h1>
            <p className="fg-lede">
              An interactive guide to <strong>14-pole Z-plane morphing filters</strong>, passive
              nonlinear resonance, anti-aliased inter-stage drive, and 14th-order Burg LPC audio
              imprinting. Play with live Web Audio models right in your browser, then trace every
              concept down to the real shipping C++20 engine.
            </p>
          </div>
          <aside className="fg-manifesto" aria-label="How to read this guide">
            <p className="fg-eyebrow" style={{ color: "oklch(0.82 0.14 318)" }}>
              Two Layers · One Instrument
            </p>
            <p>
              <strong>Layer 1 (Musician):</strong> Plain English, musical intuition, and playable
              live synth benches. Learn <em>why</em> a Z-plane filter talks like a vocal tract and
              chews through neuro bass.
            </p>
            <p>
              <strong>Layer 2 (DSP Engineer):</strong> Complex roots on the unit circle, Cytomic TPT
              equations, ADAA2 antiderivatives, and line-linked C++20 excerpts straight from{" "}
              <code>products/pole-field/</code>.
            </p>
          </aside>
        </div>
      </section>

      {/* Sticky Reading Depth & Master Audio Kill Switch */}
      <div className="fg-toolbar" role="region" aria-label="Guide reading depth and audio control">
        <div className="wrap fg-toolbar__inner">
          <div className="fg-depth-group" role="group" aria-label="Reading depth">
            {[
              ["musician", "Musician"],
              ["both", "Both Layers"],
              ["engineer", "DSP Engineer"],
            ].map(([id, label]) => (
              <button
                key={id}
                type="button"
                className="fg-depth-btn"
                aria-pressed={depth === id}
                onClick={() => setDepth(id)}
              >
                {label}
              </button>
            ))}
          </div>

          <button
            type="button"
            className={`fg-audio-pill${activeAudioLab ? " fg-audio-pill--on" : ""}`}
            onClick={() => (activeAudioLab ? stopAllAudio() : toggleLabAudio("lab3"))}
          >
            <span className="fg-led" aria-hidden="true" />
            <span>{activeAudioLab ? `Stop Audio (${audioStatus})` : "Audition Pole Field"}</span>
          </button>
        </div>
      </div>

      <div className="wrap">
        <Lab1UnitCircle {...sharedLabProps} />
        <Lab2MorphStability depth={depth} showMusician={showMusician} showEngineer={showEngineer} />
        <Lab3MorphCube {...sharedLabProps} />
        <Lab4AdaaDrive {...sharedLabProps} />
        <Lab5BurgImprint {...sharedLabProps} />

        {/* Closing Copyleft & Studio Download CTA */}
        <section className="fg-chapter" style={{ borderBlockEnd: 0 }}>
          <div className="fg-cta">
            <p className="fg-eyebrow" style={{ color: "oklch(0.82 0.14 318)" }}>
              Open Copyleft Craft · Built by Sean Dearnaley
            </p>
            <h2 style={{ fontSize: "var(--step-2)" }}>
              Read the Code, Compile It Yourself — or Grab the Signed Studio Build
            </h2>
            <div className="fg-cta__grid">
              <div style={{ display: "grid", gap: "0.75rem", alignContent: "start" }}>
                <p>
                  Everything you just played with runs straight inside{" "}
                  <strong>Pole Field</strong> as a zero-allocation{" "}
                  <code>[[clang::nonblocking]]</code> C++20 VST3, AU, and Standalone morphing
                  filterbank with a 60fps Visage GPU interface, 12 morph cubes, 42 presets,
                  audio-rate FM, and 4 drawable modulation lanes.
                </p>
                <div className="fg-chips">
                  <a className="fg-cta__btn" href="/#pole-field">
                    Download Signed macOS / Win / Linux Build
                  </a>
                  <a
                    className="fg-cta__btn fg-cta__btn--ghost"
                    href={`${REPO}/tree/main/products/pole-field`}
                  >
                    Browse Pole Field C++ on GitHub ↗
                  </a>
                </div>
              </div>
              <div className="fg-code-card">
                <div className="fg-code-card__bar">
                  <span>Compile from source (AGPL-3.0-or-later)</span>
                  <Link href="/learn">All Field Guides →</Link>
                </div>
                <pre>
                  <code>{`git clone ${REPO}.git
cd human-being-audio/products/pole-field
cmake --preset dev
cmake --build --preset dev && ctest --preset dev`}</code>
                </pre>
              </div>
            </div>
          </div>
        </section>
      </div>
    </main>
  );
}
