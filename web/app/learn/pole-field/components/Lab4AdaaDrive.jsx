"use client";

import { useEffect, useMemo, useRef, useState } from "react";
import { evaluateAdaaSpectrum } from "../dspMath";
import { drawLab4SpectrumCanvas } from "./FieldGuideCanvases";
import { CPP_ADAA, CodeExcerpt, SliderControl } from "./GuidePrimitives";

const LAB4_SPEC_W = 640;
const LAB4_SPEC_H = 250;

export default function Lab4AdaaDrive({
  depth,
  showMusician,
  showEngineer,
  activeAudioLab,
  toggleLabAudio,
  postAudioState,
}) {
  const [adaaDrive, setAdaaDrive] = useState(5.5);
  const [adaaFreq, setAdaaFreq] = useState(2130);
  const [adaaShaper, setAdaaShaper] = useState("buchla");
  const [adaaOs, setAdaaOs] = useState(2);
  const [adaaEnabled, setAdaaEnabled] = useState(true);
  const lab4SpecRef = useRef(null);

  const lab4Data = useMemo(
    () => evaluateAdaaSpectrum(adaaDrive, adaaFreq, adaaShaper, adaaOs),
    [adaaDrive, adaaFreq, adaaShaper, adaaOs],
  );

  useEffect(() => {
    postAudioState("lab4", {
      mode: "adaa",
      adaaEnabled,
      adaaDrive,
      adaaFreq,
      adaaShaper,
      adaaOs,
    });
  }, [activeAudioLab, adaaEnabled, adaaDrive, adaaFreq, adaaShaper, adaaOs, postAudioState]);

  useEffect(() => {
    drawLab4SpectrumCanvas(lab4SpecRef.current, LAB4_SPEC_W, LAB4_SPEC_H, lab4Data);
  }, [lab4Data]);

  return (
    <section className="fg-chapter" id="adaa2" aria-labelledby="ch4-title">
      <div className="fg-chapter__head">
        <span className="fg-chapter__num">04 · Clean Analog Bite Without Digital Grime</span>
        <h2 id="ch4-title">Inter-Stage Saturation &amp; Why ADAA2 Kills Aliasing</h2>
      </div>

      <div className="fg-layers" data-depth={depth}>
        {showMusician && (
          <article className="fg-layer fg-layer--musician">
            <span className="fg-layer__tag">Layer 1 · The Musical Ear</span>
            <p>
              Pushing hard distortion inside a plugin generates crisp upper harmonics — until
              those harmonics bounce off the sample-rate ceiling (24 kHz) and fold backward as
              ugly, dissonant metallic buzz (<strong>aliasing</strong>).
            </p>
            <p>
              In Lab 4 below, sweep the frequency or turn up Drive and watch the{" "}
              <strong style={{ color: "oklch(0.74 0.16 18)" }}>red naive spectrum</strong> fill
              with inharmonic hash between the real overtones, while{" "}
              <strong style={{ color: "oklch(0.76 0.15 156)" }}>ADAA2</strong> carves that hash
              down by 15–30 dB <em>before</em> oversampling even kicks in.
            </p>
          </article>
        )}
        {showEngineer && (
          <article className="fg-layer fg-layer--engineer">
            <span className="fg-layer__tag">Layer 2 · Under the Hood (Continuous-Time Antiderivatives)</span>
            <p>
              Instead of evaluating a waveshaper <code>f(x[n])</code> at discrete instants,
              second-order Antiderivative Anti-Aliasing (Parker/Bilbao et al., DAFx-16/17)
              analytically integrates the waveshaper twice over a triangular kernel using its
              closed-form antiderivatives <code>F₁</code> and <code>F₂</code>, paired with HIIR
              2×/4×/8× polyphase oversampling in <code>modules/dsp/include/hba/dsp/adaa.h</code>.
            </p>
            <div className="fg-formula">
              ADAA1: y[n] = (F₁(x[n]) − F₁(x[n−1])) / (x[n] − x[n−1])
            </div>
          </article>
        )}
      </div>

      <div className="fg-bench" aria-label="ADAA2 aliasing comparison lab">
        <div className="fg-bench__bar">
          <span className="fg-bench__title">
            Lab 4 · Red = Naive Waveshaper · Emerald = ADAA Anti-Aliased (48 kHz)
          </span>
          <div className="fg-chips">
            <button
              type="button"
              className="fg-chip fg-chip--play"
              aria-pressed={activeAudioLab === "lab4"}
              onClick={() => toggleLabAudio("lab4")}
            >
              {activeAudioLab === "lab4" ? "■ Stop Tone" : "▶ Hear Aliasing A/B"}
            </button>
            <button
              type="button"
              className="fg-chip"
              aria-pressed={adaaEnabled}
              onClick={() => setAdaaEnabled((v) => !v)}
            >
              Audio Mode: {adaaEnabled ? "ADAA ON (Clean)" : "NAIVE (Aliased)"}
            </button>
          </div>
        </div>

        <div className="fg-bench__body fg-bench__body--wide">
          <div className="fg-bench__stage">
            <div className="fg-canvas-box">
              <span className="fg-canvas-label">FFT Spectrum (0 – 24 kHz, −96 to 0 dB)</span>
              <canvas
                ref={lab4SpecRef}
                width={LAB4_SPEC_W}
                height={LAB4_SPEC_H}
                aria-label="FFT spectrum comparing Naive and ADAA2 aliasing peaks"
              />
            </div>
            <div className="fg-readout">
              <div className="fg-stat">
                <span className="fg-stat__k">Worst Naive Alias</span>
                <span className="fg-stat__v" style={{ color: "oklch(0.74 0.16 18)" }}>
                  {lab4Data.naiveAliasDb.toFixed(1)} dB
                </span>
              </div>
              <div className="fg-stat">
                <span className="fg-stat__k">Worst ADAA Alias</span>
                <span className="fg-stat__v" style={{ color: "oklch(0.78 0.15 156)" }}>
                  {lab4Data.adaaAliasDb.toFixed(1)} dB
                </span>
              </div>
              <div className="fg-stat">
                <span className="fg-stat__k">Alias Reduction</span>
                <span className="fg-stat__v" style={{ color: "oklch(0.80 0.14 202)" }}>
                  {lab4Data.reductionDb >= 0 ? `-${lab4Data.reductionDb.toFixed(1)} dB` : "0 dB"}
                </span>
              </div>
            </div>
          </div>

          <div className="fg-bench__controls">
            <div>
              <span className="fg-stat__k" style={{ display: "block", marginBlockEnd: "0.35rem" }}>
                Waveshaper Curve
              </span>
              <div className="fg-chips">
                {[
                  ["buchla", "Buchla 259 Fold"],
                  ["clip", "Hard Clip"],
                  ["tanh", "Tanh Tube"],
                ].map(([id, label]) => (
                  <button
                    key={id}
                    type="button"
                    className="fg-chip"
                    aria-pressed={adaaShaper === id}
                    onClick={() => setAdaaShaper(id)}
                  >
                    {label}
                  </button>
                ))}
              </div>
            </div>
            <div>
              <span className="fg-stat__k" style={{ display: "block", marginBlockEnd: "0.35rem" }}>
                ADAA2 Oversampling Rate
              </span>
              <div className="fg-chips">
                {[
                  [1, "1× ADAA2 Only"],
                  [2, "2× OS + ADAA2 (Pole Field)"],
                ].map(([rate, label]) => (
                  <button
                    key={rate}
                    type="button"
                    className="fg-chip"
                    aria-pressed={adaaOs === rate}
                    onClick={() => setAdaaOs(rate)}
                  >
                    {label}
                  </button>
                ))}
              </div>
            </div>
            <SliderControl
              id="l4-drive"
              label="Input Drive"
              min={1.2}
              max={10}
              step={0.1}
              value={adaaDrive}
              displayValue={`${adaaDrive.toFixed(1)}×`}
              onChange={setAdaaDrive}
            />
            <SliderControl
              id="l4-freq"
              label="Test Sine Frequency"
              min={420}
              max={5400}
              step={10}
              value={adaaFreq}
              displayValue={`${adaaFreq} Hz`}
              onChange={setAdaaFreq}
            />
          </div>
        </div>
      </div>

      {showEngineer && (
        <CodeExcerpt
          file="modules/dsp/include/hba/dsp/adaa.h"
          lines="101-127"
          code={CPP_ADAA}
        />
      )}
    </section>
  );
}
