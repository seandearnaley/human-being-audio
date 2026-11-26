"use client";

import { useEffect, useMemo, useRef, useState } from "react";
import { SAMPLE_RATE, compareCartesianVsPolar, evaluateStageH } from "../dspMath";
import { drawBodeCanvas, drawLab2TrajectoryCanvas } from "./FieldGuideCanvases";
import { CPP_POLAR, CodeExcerpt, SliderControl } from "./GuidePrimitives";

const LAB2_Z_W = 320;
const LAB2_Z_H = 240;
const LAB2_BODE_W = 420;
const LAB2_BODE_H = 240;

export default function Lab2MorphStability({ depth, showMusician, showEngineer }) {
  const [morphT, setMorphT] = useState(0.5);
  const lab2ZRef = useRef(null);
  const lab2BodeRef = useRef(null);

  const lab2Data = useMemo(() => compareCartesianVsPolar(morphT), [morphT]);

  useEffect(() => {
    drawLab2TrajectoryCanvas(lab2ZRef.current, LAB2_Z_W, LAB2_Z_H, lab2Data);
    drawBodeCanvas(
      lab2BodeRef.current,
      LAB2_BODE_W,
      LAB2_BODE_H,
      (hz) => {
        const st = {
          poleHz: lab2Data.polar.hz,
          poleQ: lab2Data.polar.q,
          zeroHz: lab2Data.polar.hz * 0.45,
          zeroQ: 1.2,
        };
        const h = evaluateStageH(st, hz, SAMPLE_RATE);
        return 20 * Math.log10(Math.max(Math.hypot(h[0], h[1]), 1e-4));
      },
      "#38d9a9",
      (hz) => {
        const st = {
          poleHz: lab2Data.cartesian.hz,
          poleQ: lab2Data.cartesian.q,
          zeroHz: lab2Data.cartesian.hz * 0.45,
          zeroQ: 1.2,
        };
        const h = evaluateStageH(st, hz, SAMPLE_RATE);
        return 20 * Math.log10(Math.max(Math.hypot(h[0], h[1]), 1e-4));
      },
    );
  }, [lab2Data]);

  return (
    <section className="fg-chapter" id="polar-stability" aria-labelledby="ch2-title">
      <div className="fg-chapter__head">
        <span className="fg-chapter__num">02 · The E-MU Morpheus Secret</span>
        <h2 id="ch2-title">Why Naive Filter Morphs Lose Their Bite — and Log-Polar Wins</h2>
      </div>

      <div className="fg-layers" data-depth={depth}>
        {showMusician && (
          <article className="fg-layer fg-layer--musician">
            <span className="fg-layer__tag">Layer 1 · The Musical Ear</span>
            <p>
              Ever notice how crossfading two ordinary EQ presets sounds lifeless in the middle,
              or why modulating classic digital filters fast makes them click or blow up?
            </p>
            <p>
              If you simply blend two filter formulas in a straight line, the resonant peaks
              cut straight across the dull center of the circle — <strong>losing almost all their
              resonance halfway through the sweep</strong>. Pole Field glides every pole along a
              curved musical arc (logarithmic frequency and Q), so your formant stays razor-sharp
              and musical at every single millimeter between corners.
            </p>
          </article>
        )}
        {showEngineer && (
          <article className="fg-layer fg-layer--engineer">
            <span className="fg-layer__tag">Layer 2 · Under the Hood (Log-Polar Trilinear Morph)</span>
            <p>
              Interpolating direct-form biquad coefficients <code>(a₁, a₂)</code> or Cartesian
              root coordinates <code>(x, y)</code> cuts a straight chord inside the unit circle:
              the radius <code>|z|</code> collapses at <code>t = 0.5</code> (and rapid modulation
              of direct-form delay states injects energy spikes that blow up past{" "}
              <code>|z| ≥ 1</code>).
            </p>
            <p>
              Pole Field stores all 8 corners of every 3D cube in continuous{" "}
              <code>(poleHz, poleQ, zeroHz, zeroQ)</code>, trilinearly interpolates in{" "}
              <strong>log-frequency and log-Q</strong> (<code>logTrilerp</code>), and smooths
              each stage in log-polar space over 6 ms inside the TPT SVF.
            </p>
          </article>
        )}
      </div>

      <div className="fg-bench" aria-label="Polar versus Cartesian interpolation lab">
        <div className="fg-bench__bar">
          <span className="fg-bench__title">
            Lab 2 · Scrub t (220 Hz, Q=26 → 5,200 Hz, Q=26) · Emerald = Log-Polar · Red Dashed = Cartesian
          </span>
        </div>

        <div className="fg-bench__body fg-bench__body--wide">
          <div className="fg-bench__stage">
            <div className="fg-canvas-pair">
              <div className="fg-canvas-box">
                <span className="fg-canvas-label">Pole Trajectory Inside |z| = 1</span>
                <canvas
                  ref={lab2ZRef}
                  width={LAB2_Z_W}
                  height={LAB2_Z_H}
                  aria-label="Comparison of curved Log-Polar arc versus straight Cartesian chord"
                />
              </div>
              <div className="fg-canvas-box">
                <span className="fg-canvas-label">Resulting Peak at Current Morph t</span>
                <canvas
                  ref={lab2BodeRef}
                  width={LAB2_BODE_W}
                  height={LAB2_BODE_H}
                  aria-label="Bode plot comparing Log-Polar peak preservation against Cartesian Q collapse"
                />
              </div>
            </div>
          </div>

          <div className="fg-bench__controls">
            <SliderControl
              id="l2-t"
              label="Morph Position (t)"
              min={0}
              max={1}
              step={0.01}
              value={morphT}
              displayValue={`${Math.round(morphT * 100)}%`}
              onChange={setMorphT}
            />
            <div className="fg-readout">
              <div className="fg-stat">
                <span className="fg-stat__k">Log-Polar Q</span>
                <span className="fg-stat__v" style={{ color: "oklch(0.78 0.15 156)" }}>
                  Q {lab2Data.polar.q.toFixed(1)}
                </span>
              </div>
              <div className="fg-stat">
                <span className="fg-stat__k">Cartesian Q</span>
                <span className="fg-stat__v" style={{ color: "oklch(0.74 0.16 18)" }}>
                  Q {lab2Data.cartesian.q.toFixed(1)}
                </span>
              </div>
              <div className="fg-stat">
                <span className="fg-stat__k">Log-Polar Freq</span>
                <span className="fg-stat__v">{Math.round(lab2Data.polar.hz)} Hz</span>
              </div>
              <div className="fg-stat">
                <span className="fg-stat__k">Cartesian |z|</span>
                <span className="fg-stat__v">{lab2Data.cartesian.r.toFixed(3)}</span>
              </div>
            </div>
            <p style={{ fontSize: "0.78rem", color: "var(--screen-dim)", lineHeight: 1.48 }}>
              At <code>t = 50%</code>, Cartesian interpolation drops resonance from{" "}
              <strong>Q = 26</strong> all the way down to <strong>Q ≈ 2</strong> — flattening
              the talking peak completely. Log-Polar stays locked at <strong>Q = 26</strong>{" "}
              through the entire musical sweep!
            </p>
          </div>
        </div>
      </div>

      {showEngineer && (
        <CodeExcerpt
          file="products/pole-field/core/src/lattice_eval.cpp"
          lines="145-190"
          code={CPP_POLAR}
        />
      )}
    </section>
  );
}
