"use client";

import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import {
  SAMPLE_RATE,
  evaluateMagnitudeDb,
  generateSyntheticImprintSource,
  resolveLattice,
  runBurgLpcImprint,
} from "../dspMath";
import { drawBodeCanvas, drawZPlaneCanvas } from "./FieldGuideCanvases";
import { CPP_BURG, CodeExcerpt } from "./GuidePrimitives";

const LAB5_Z_W = 300;
const LAB5_Z_H = 240;
const LAB5_BODE_W = 420;
const LAB5_BODE_H = 240;

export default function Lab5BurgImprint({
  depth,
  showMusician,
  showEngineer,
  activeAudioLab,
  toggleLabAudio,
  postAudioState,
}) {
  const [imprintKind, setImprintKind] = useState("vowel");
  const [imprintLabel, setImprintLabel] = useState("Human 'Ah' Vowel (built-in)");
  const [imprintCorner, setImprintCorner] = useState(() => {
    const src = generateSyntheticImprintSource("vowel");
    return runBurgLpcImprint(src.samples, src.sampleRate);
  });
  const lab5ZRef = useRef(null);
  const lab5BodeRef = useRef(null);

  const lab5Lattice = useMemo(
    () =>
      resolveLattice({
        cubeIndex: 0,
        resAmount: 0.52,
        activeStages: 7,
        topology: 0,
        customCorner: imprintCorner,
      }),
    [imprintCorner],
  );

  useEffect(() => {
    postAudioState("lab5", {
      mode: "lattice",
      source: "reese",
      topology: 0,
      activeStages: 7,
      drive: 0.28,
      drivePos: 1,
      subLock: true,
      makeup: lab5Lattice.makeupLinear,
      stages: lab5Lattice.stages,
    });
  }, [activeAudioLab, lab5Lattice, postAudioState]);

  useEffect(() => {
    drawZPlaneCanvas(lab5ZRef.current, LAB5_Z_W, LAB5_Z_H, lab5Lattice.stages, 7);
    drawBodeCanvas(
      lab5BodeRef.current,
      LAB5_BODE_W,
      LAB5_BODE_H,
      (hz) => evaluateMagnitudeDb(lab5Lattice, 7, 0, hz, SAMPLE_RATE),
      "#66d9e8",
    );
  }, [lab5Lattice]);

  const handleSelectSyntheticImprint = useCallback((kind, label) => {
    setImprintKind(kind);
    setImprintLabel(label);
    const src = generateSyntheticImprintSource(kind);
    setImprintCorner(runBurgLpcImprint(src.samples, src.sampleRate));
  }, []);

  const handleAudioUpload = useCallback(async (e) => {
    const file = e.target.files?.[0];
    e.target.value = "";
    if (!file) return;
    let ctx = null;
    try {
      const buf = await file.arrayBuffer();
      const Ctx = window.AudioContext || window.webkitAudioContext;
      ctx = new Ctx();
      const decoded = await ctx.decodeAudioData(buf);
      const ch0 = decoded.getChannelData(0);
      const corner = runBurgLpcImprint(ch0, decoded.sampleRate);
      setImprintKind("upload");
      setImprintLabel(`Imprinted: ${file.name} (${decoded.sampleRate} Hz)`);
      setImprintCorner(corner);
    } catch {
      setImprintLabel("Could not decode audio file — try WAV, MP3, or FLAC.");
    } finally {
      ctx?.close().catch(() => {});
    }
  }, []);

  return (
    <section className="fg-chapter" id="burg-lpc" aria-labelledby="ch5-title">
      <div className="fg-chapter__head">
        <span className="fg-chapter__num">05 · Steal the Body of Any Sound</span>
        <h2 id="ch5-title">14th-Order Burg LPC Imprinting — Live in Your Browser</h2>
      </div>

      <div className="fg-layers" data-depth={depth}>
        {showMusician && (
          <article className="fg-layer fg-layer--musician">
            <span className="fg-layer__tag">Layer 1 · The Musical Ear</span>
            <p>
              Every acoustic sound — a shouted vowel, a metallic snare rim, a tuba blast, a
              struck wine glass — has a hidden fingerprint of resonant peaks. Pole Field lets
              you drop <em>any WAV sample</em> onto a cube corner and stamp its exact 14-pole
              resonant body onto your synths or drums.
            </p>
            <p>
              Try the built-in targets below or drop any audio file from your drive (processed
              100% locally in your browser — nothing is uploaded), then hit{" "}
              <strong>Audition Imprinted Lattice</strong> to play a Neuro Reese right through
              its extracted formants.
            </p>
          </article>
        )}
        {showEngineer && (
          <article className="fg-layer fg-layer--engineer">
            <span className="fg-layer__tag">Layer 2 · Under the Hood (Burg’s Method + Durand-Kerner)</span>
            <p>
              Pole Field picks the highest sustained tonal frame (penalizing brief transient
              clicks via crest factor), decimates cleanly to ~24 kHz, fits a{" "}
              <strong>14th-order all-pole AR model via Burg’s lattice recursion</strong> (whose
              reflection coefficients <code>|k_m| &lt; 1</code> guarantee minimum-phase
              stability!), and solves all 14 complex polynomial roots simultaneously with{" "}
              <strong>Durand-Kerner iteration</strong> before placing alternating dipole zeros.
            </p>
          </article>
        )}
      </div>

      <div className="fg-bench" aria-label="Live 14th-order Burg LPC audio imprinting lab">
        <div className="fg-bench__bar">
          <span className="fg-bench__title">Lab 5 · {imprintLabel}</span>
          <div className="fg-chips">
            <button
              type="button"
              className="fg-chip fg-chip--play"
              aria-pressed={activeAudioLab === "lab5"}
              onClick={() => toggleLabAudio("lab5")}
            >
              {activeAudioLab === "lab5" ? "■ Stop Imprint" : "▶ Audition Imprinted Lattice"}
            </button>
          </div>
        </div>

        <div className="fg-bench__body fg-bench__body--wide">
          <div className="fg-bench__stage">
            <div className="fg-canvas-pair">
              <div className="fg-canvas-box">
                <span className="fg-canvas-label">Extracted 14-Pole Roots</span>
                <canvas
                  ref={lab5ZRef}
                  width={LAB5_Z_W}
                  height={LAB5_Z_H}
                  aria-label="Z-plane roots extracted by 14th-order Burg LPC"
                />
              </div>
              <div className="fg-canvas-box">
                <span className="fg-canvas-label">Imprinted Filter Response</span>
                <canvas
                  ref={lab5BodeRef}
                  width={LAB5_BODE_W}
                  height={LAB5_BODE_H}
                  aria-label="Frequency response of imprinted filter"
                />
              </div>
            </div>
          </div>

          <div className="fg-bench__controls">
            <div>
              <span className="fg-stat__k" style={{ display: "block", marginBlockEnd: "0.35rem" }}>
                Built-In Targets (Runs Real Burg LPC Live)
              </span>
              <div className="fg-chips">
                {[
                  ["vowel", "Human 'Ah' Vowel"],
                  ["snare", "Metallic Snare"],
                  ["brass", "Brass Growl"],
                  ["bell", "Glass Bell"],
                ].map(([id, label]) => (
                  <button
                    key={id}
                    type="button"
                    className="fg-chip"
                    aria-pressed={imprintKind === id}
                    onClick={() => handleSelectSyntheticImprint(id, `${label} (built-in)`)}
                  >
                    {label}
                  </button>
                ))}
              </div>
            </div>

            <label className="fg-dropzone">
              <span>Or imprint your own audio file (100% local, zero upload):</span>
              <input type="file" accept="audio/*" onChange={handleAudioUpload} />
            </label>

            <div className="fg-readout">
              {imprintCorner.stages.slice(0, 4).map((st, idx) => (
                <div key={idx} className="fg-stat">
                  <span className="fg-stat__k">Formant F{idx + 1}</span>
                  <span className="fg-stat__v">
                    {Math.round(st.poleHz)} Hz · Q{st.poleQ.toFixed(1)}
                  </span>
                </div>
              ))}
            </div>
          </div>
        </div>
      </div>

      {showEngineer && (
        <CodeExcerpt
          file="products/pole-field/core/src/imprint_lpc.cpp"
          lines="126-160"
          code={CPP_BURG}
        />
      )}
    </section>
  );
}
