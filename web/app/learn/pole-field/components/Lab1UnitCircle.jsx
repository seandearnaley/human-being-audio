"use client";

import { useCallback, useEffect, useRef, useState } from "react";
import { SAMPLE_RATE, evaluateStageH, stageZPoint, zPointToHzQ } from "../dspMath";
import { drawBodeCanvas, drawZPlaneCanvas } from "./FieldGuideCanvases";
import { CPP_DIPOLE, CodeExcerpt, SliderControl } from "./GuidePrimitives";

const LAB1_Z_W = 320;
const LAB1_Z_H = 260;
const LAB1_BODE_W = 420;
const LAB1_BODE_H = 260;

export default function Lab1UnitCircle({
  depth,
  showMusician,
  showEngineer,
  activeAudioLab,
  toggleLabAudio,
  postAudioState,
}) {
  const [dipole, setDipole] = useState({ poleHz: 780, poleQ: 7.5, zeroHz: 1450, zeroQ: 5.5 });
  const [dragTarget, setDragTarget] = useState(null);
  const lab1ZRef = useRef(null);
  const lab1BodeRef = useRef(null);

  useEffect(() => {
    postAudioState("lab1", {
      mode: "single",
      source: "reese",
      drive: 0.15,
      drivePos: 2,
      subLock: false,
      makeup: 0.9,
      stages: [dipole],
    });
  }, [activeAudioLab, dipole, postAudioState]);

  useEffect(() => {
    drawZPlaneCanvas(lab1ZRef.current, LAB1_Z_W, LAB1_Z_H, [dipole], 1, 0);
    drawBodeCanvas(lab1BodeRef.current, LAB1_BODE_W, LAB1_BODE_H, (hz) => {
      const h = evaluateStageH(dipole, hz, SAMPLE_RATE);
      return 20 * Math.log10(Math.max(Math.hypot(h[0], h[1]), 1e-4));
    });
  }, [dipole]);

  const handleLab1Pointer = useCallback(
    (e, isDown) => {
      const canvas = lab1ZRef.current;
      if (!canvas) return;
      const rect = canvas.getBoundingClientRect();
      // Map pointer into logical CSS canvas space (independent of Retina DPR backing store)
      const x = ((e.clientX - rect.left) / Math.max(1, rect.width)) * LAB1_Z_W;
      const y = ((e.clientY - rect.top) / Math.max(1, rect.height)) * LAB1_Z_H;
      const cx = LAB1_Z_W * 0.5;
      const cy = LAB1_Z_H * 0.56;
      const R = Math.min(LAB1_Z_W * 0.32, LAB1_Z_H * 0.35);

      const dx = (x - cx) / R;
      const dy = -(y - cy) / R;
      const r = Math.min(0.992, Math.max(0.12, Math.hypot(dx, dy)));
      const theta = Math.max(0.03, Math.min(Math.PI - 0.05, Math.abs(Math.atan2(dy, dx))));

      let target = dragTarget;
      if (isDown) {
        const pt = stageZPoint(dipole, SAMPLE_RATE);
        const px = cx + pt.poleR * R * Math.cos(pt.poleTheta);
        const pyPos = cy - pt.poleR * R * Math.sin(pt.poleTheta);
        const pyNeg = cy + pt.poleR * R * Math.sin(pt.poleTheta);
        const zx = cx + pt.zeroR * R * Math.cos(pt.zeroTheta);
        const zyPos = cy - pt.zeroR * R * Math.sin(pt.zeroTheta);
        const zyNeg = cy + pt.zeroR * R * Math.sin(pt.zeroTheta);
        const dPole = Math.min(Math.hypot(x - px, y - pyPos), Math.hypot(x - px, y - pyNeg));
        const dZero = Math.min(Math.hypot(x - zx, y - zyPos), Math.hypot(x - zx, y - zyNeg));
        target = dZero < dPole ? "zero" : "pole";
        setDragTarget(target);
        canvas.setPointerCapture?.(e.pointerId);
      }
      if (!target) return;
      const { hz, q } = zPointToHzQ(r, theta, SAMPLE_RATE);
      setDipole((prev) =>
        target === "pole"
          ? {
              ...prev,
              poleHz: Math.min(8500, Math.max(80, Math.round(hz))),
              poleQ: Number(Math.min(36, Math.max(0.7, q)).toFixed(1)),
            }
          : {
              ...prev,
              zeroHz: Math.min(10500, Math.max(80, Math.round(hz))),
              zeroQ: Number(Math.min(20, Math.max(0.7, q)).toFixed(1)),
            },
      );
    },
    [dipole, dragTarget],
  );

  const lab1Pt = stageZPoint(dipole, SAMPLE_RATE);

  return (
    <section className="fg-chapter" id="unit-circle" aria-labelledby="ch1-title">
      <div className="fg-chapter__head">
        <span className="fg-chapter__num">01 · Interactive Intuition</span>
        <h2 id="ch1-title">Touch the Z-Plane: Poles Push Up, Zeros Pin Down</h2>
      </div>

      <div className="fg-layers" data-depth={depth}>
        {showMusician && (
          <article className="fg-layer fg-layer--musician">
            <span className="fg-layer__tag">Layer 1 · The Musical Ear</span>
            <p>
              Picture a taut rubber sheet stretched over a circle. Every frequency from sub-bass
              (right edge, <code>0 Hz</code>) to ultra-highs (left edge, Nyquist) wraps around
              the rim.
            </p>
            <p>
              A <strong>Pole (×)</strong> is a tent pole pushing the sheet upward into a singing
              resonant formant. Push it closer to the outer rim and the peak gets sharper until
              it whistles. A <strong>Zero (○)</strong> is a tent stake pinning the sheet down
              into a deep phaser notch. Pair them side by side — a <strong>dipole</strong> — and
              you get that liquid, talking throat bite that ordinary low-pass filters can never
              produce.
            </p>
          </article>
        )}
        {showEngineer && (
          <article className="fg-layer fg-layer--engineer">
            <span className="fg-layer__tag">Layer 2 · Under the Hood (TPT Cytomic SVF)</span>
            <p>
              Each stage in Pole Field is a 2nd-order continuous-time pole-zero section mapped
              via the bilinear transform <code>z = (1 + s_c) / (1 − s_c)</code>, evaluated in
              Topology-Preserving Transform (Cytomic SVF) form so modulation stays click-free at
              audio rate.
            </p>
            <div className="fg-formula">
              H(s) = norm · (s² + r·k_z·s + r²) / (s² + k_p·s + 1),&nbsp;&nbsp;r = tan(πf_z/f_s)
              / tan(πf_p/f_s)
            </div>
            <p>
              To let resonance sing cleanly up to <code>Q = 45</code> without numeric blowups or
              harsh digital limit cycles, Pole Field injects <strong>passive nonlinear OTA
              damping</strong> directly into the feedback term{" "}
              <code>k_add = 0.28 · e² / (1 + e²)</code>.
            </p>
          </article>
        )}
      </div>

      <div className="fg-bench" aria-label="Single dipole Z-plane playground">
        <div className="fg-bench__bar">
          <span className="fg-bench__title">Lab 1 · Drag Pole (×) or Zero (○) Inside the Circle</span>
          <div className="fg-chips">
            <button
              type="button"
              className="fg-chip fg-chip--play"
              aria-pressed={activeAudioLab === "lab1"}
              onClick={() => toggleLabAudio("lab1")}
            >
              {activeAudioLab === "lab1" ? "■ Stop Reese" : "▶ Play Live Dipole"}
            </button>
            <button
              type="button"
              className="fg-chip"
              onClick={() => setDipole({ poleHz: 780, poleQ: 7.5, zeroHz: 1450, zeroQ: 5.5 })}
            >
              Reset Dipole
            </button>
          </div>
        </div>

        <div className="fg-bench__body fg-bench__body--wide">
          <div className="fg-bench__stage">
            <div className="fg-canvas-pair">
              <div className="fg-canvas-box fg-canvas-box--interactive">
                <span className="fg-canvas-label">Z-Plane Unit Circle (drag × / ○)</span>
                <canvas
                  ref={lab1ZRef}
                  width={LAB1_Z_W}
                  height={LAB1_Z_H}
                  aria-label="Interactive Z-plane unit circle; drag pole or zero"
                  onPointerDown={(e) => handleLab1Pointer(e, true)}
                  onPointerMove={(e) => handleLab1Pointer(e, false)}
                  onPointerUp={() => setDragTarget(null)}
                  onPointerCancel={() => setDragTarget(null)}
                />
              </div>
              <div className="fg-canvas-box">
                <span className="fg-canvas-label">Magnitude Response (25 Hz – 18 kHz)</span>
                <canvas
                  ref={lab1BodeRef}
                  width={LAB1_BODE_W}
                  height={LAB1_BODE_H}
                  aria-label="Live dipole frequency response curve"
                />
              </div>
            </div>

            <div className="fg-readout">
              <div className="fg-stat">
                <span className="fg-stat__k">Pole Radius |z_p|</span>
                <span className="fg-stat__v">{lab1Pt.poleR.toFixed(4)}</span>
              </div>
              <div className="fg-stat">
                <span className="fg-stat__k">Pole Angle θ_p</span>
                <span className="fg-stat__v">{((lab1Pt.poleTheta * 180) / Math.PI).toFixed(1)}°</span>
              </div>
              <div className="fg-stat">
                <span className="fg-stat__k">Zero Radius |z_z|</span>
                <span className="fg-stat__v">{lab1Pt.zeroR.toFixed(4)}</span>
              </div>
              <div className="fg-stat">
                <span className="fg-stat__k">Stability Margin</span>
                <span className="fg-stat__v" style={{ color: "oklch(0.78 0.15 156)" }}>
                  1 − |z| = {(1 - lab1Pt.poleR).toFixed(4)}
                </span>
              </div>
            </div>
          </div>

          <div className="fg-bench__controls">
            <SliderControl
              id="l1-phz"
              label="Pole Frequency (×)"
              min={80}
              max={8500}
              step={10}
              value={dipole.poleHz}
              displayValue={`${dipole.poleHz} Hz`}
              onChange={(v) => setDipole((d) => ({ ...d, poleHz: v }))}
            />
            <SliderControl
              id="l1-pq"
              label="Pole Resonance (Q)"
              min={0.7}
              max={36}
              step={0.1}
              value={dipole.poleQ}
              displayValue={`Q ${dipole.poleQ.toFixed(1)}`}
              onChange={(v) => setDipole((d) => ({ ...d, poleQ: v }))}
            />
            <SliderControl
              id="l1-zhz"
              label="Zero Notch Frequency (○)"
              min={80}
              max={10500}
              step={10}
              value={dipole.zeroHz}
              displayValue={`${dipole.zeroHz} Hz`}
              onChange={(v) => setDipole((d) => ({ ...d, zeroHz: v }))}
            />
            <SliderControl
              id="l1-zq"
              label="Zero Notch Sharpness"
              min={0.7}
              max={20}
              step={0.1}
              value={dipole.zeroQ}
              displayValue={`Q ${dipole.zeroQ.toFixed(1)}`}
              onChange={(v) => setDipole((d) => ({ ...d, zeroQ: v }))}
            />
          </div>
        </div>
      </div>

      {showEngineer && (
        <CodeExcerpt
          file="products/pole-field/engine/src/pole_zero.h"
          lines="29-91"
          code={CPP_DIPOLE}
        />
      )}
    </section>
  );
}
