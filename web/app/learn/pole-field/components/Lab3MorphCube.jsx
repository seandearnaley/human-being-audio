"use client";

import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { FACTORY_CUBES, SAMPLE_RATE, evaluateMagnitudeDb, resolveLattice } from "../dspMath";
import {
  drawBodeCanvas,
  drawLab3MorphPadCanvas,
  drawZPlaneCanvas,
} from "./FieldGuideCanvases";
import { CPP_SUBLOCK, CodeExcerpt, RECIPES, SliderControl } from "./GuidePrimitives";

const LAB3_PAD_W = 520;
const LAB3_PAD_H = 210;
const LAB3_Z_W = 260;
const LAB3_Z_H = 220;
const LAB3_BODE_W = 360;
const LAB3_BODE_H = 220;

export default function Lab3MorphCube({
  depth,
  showMusician,
  showEngineer,
  activeAudioLab,
  toggleLabAudio,
  postAudioState,
}) {
  const [cubeIndex, setCubeIndex] = useState(0);
  const [morphX, setMorphX] = useState(0.24);
  const [morphY, setMorphY] = useState(0.32);
  const [morphZ, setMorphZ] = useState(0.15);
  const [res, setRes] = useState(0.55);
  const [topology, setTopology] = useState(0);
  const [stagesCount, setStagesCount] = useState(7);
  const [drive, setDrive] = useState(0.38);
  const [drivePos, setDrivePos] = useState(1);
  const [subLock, setSubLock] = useState(true);
  const [audioSource, setAudioSource] = useState("reese");
  const lab3PadRef = useRef(null);
  const lab3ZRef = useRef(null);
  const lab3BodeRef = useRef(null);
  const padDraggingRef = useRef(false);

  const lab3Lattice = useMemo(
    () =>
      resolveLattice({
        cubeIndex,
        morphX,
        morphY,
        morphZ,
        resAmount: res,
        activeStages: stagesCount,
        topology,
      }),
    [cubeIndex, morphX, morphY, morphZ, res, stagesCount, topology],
  );

  useEffect(() => {
    postAudioState("lab3", {
      mode: "lattice",
      source: audioSource,
      topology,
      activeStages: stagesCount,
      drive,
      drivePos,
      subLock,
      makeup: lab3Lattice.makeupLinear,
      stages: lab3Lattice.stages,
    });
  }, [
    activeAudioLab,
    audioSource,
    topology,
    stagesCount,
    drive,
    drivePos,
    subLock,
    lab3Lattice,
    postAudioState,
  ]);

  useEffect(() => {
    drawLab3MorphPadCanvas(
      lab3PadRef.current,
      LAB3_PAD_W,
      LAB3_PAD_H,
      cubeIndex,
      morphX,
      morphY,
      morphZ,
    );
    drawZPlaneCanvas(lab3ZRef.current, LAB3_Z_W, LAB3_Z_H, lab3Lattice.stages, stagesCount);
    drawBodeCanvas(lab3BodeRef.current, LAB3_BODE_W, LAB3_BODE_H, (hz) =>
      evaluateMagnitudeDb(lab3Lattice, stagesCount, topology, hz, SAMPLE_RATE),
    );
  }, [cubeIndex, morphX, morphY, morphZ, lab3Lattice, stagesCount, topology]);

  const handlePadPointer = useCallback((e, isDown) => {
    const pad = lab3PadRef.current;
    if (!pad) return;
    if (isDown) {
      padDraggingRef.current = true;
      pad.setPointerCapture?.(e.pointerId);
    }
    if (!padDraggingRef.current) return;
    const rect = pad.getBoundingClientRect();
    // Map pointer into logical CSS canvas space (independent of Retina DPR backing store)
    const cx = ((e.clientX - rect.left) / Math.max(1, rect.width)) * LAB3_PAD_W;
    const cy = ((e.clientY - rect.top) / Math.max(1, rect.height)) * LAB3_PAD_H;
    const nx = Math.max(0, Math.min(1, (cx - 14) / Math.max(1, LAB3_PAD_W - 28)));
    const ny = Math.max(0, Math.min(1, (cy - 44) / Math.max(1, LAB3_PAD_H - 64)));
    setMorphX(Number(nx.toFixed(3)));
    setMorphY(Number(ny.toFixed(3)));
  }, []);

  const handlePadKeyDown = useCallback((e) => {
    const step = e.shiftKey ? 0.01 : 0.05;
    if (e.key === "ArrowLeft") {
      e.preventDefault();
      setMorphX((v) => Number(Math.max(0, v - step).toFixed(3)));
    } else if (e.key === "ArrowRight") {
      e.preventDefault();
      setMorphX((v) => Number(Math.min(1, v + step).toFixed(3)));
    } else if (e.key === "ArrowUp") {
      e.preventDefault();
      setMorphY((v) => Number(Math.max(0, v - step).toFixed(3)));
    } else if (e.key === "ArrowDown") {
      e.preventDefault();
      setMorphY((v) => Number(Math.min(1, v + step).toFixed(3)));
    } else if (e.key === "Home") {
      e.preventDefault();
      setMorphX(0);
      setMorphY(0);
    } else if (e.key === "End") {
      e.preventDefault();
      setMorphX(1);
      setMorphY(1);
    }
  }, []);

  return (
    <section className="fg-chapter" id="morph-cube" aria-labelledby="ch3-title">
      <div className="fg-chapter__head">
        <span className="fg-chapter__num">03 · The Full 14-Pole Engine Live in Your Browser</span>
        <h2 id="ch3-title">Seven Dipoles, Three Topologies, One Sub-Locked Cube</h2>
      </div>

      <div className="fg-layers" data-depth={depth}>
        {showMusician && (
          <article className="fg-layer fg-layer--musician">
            <span className="fg-layer__tag">Layer 1 · The Musical Ear</span>
            <p>
              Stacking <strong>7 pole-zero dipoles</strong> (14 poles + 14 zeros) lets a single
              XY gesture sculpt entire vocal tracts, razor phasers, and metallic ridges at once.
              Click <strong>Play 14-Pole Lattice</strong> below and drag inside the Morph Pad —
              then flip <strong>Sub Lock</strong> off and back on to hear how protecting the
              mono fundamental below 105 Hz keeps neuro bass club-solid even under extreme
              filtering.
            </p>
          </article>
        )}
        {showEngineer && (
          <article className="fg-layer fg-layer--engineer">
            <span className="fg-layer__tag">Layer 2 · Under the Hood (Alternating Dipoles &amp; LR4 Sub Lock)</span>
            <p>
              Notice two subtle engineering decisions inside Pole Field’s real C++:
              <br />
              1. <strong>Alternating dipole orientation:</strong> even stages place the zero{" "}
              <em>above</em> the pole; odd stages place it <em>below</em>, so cumulative shelf
              tilts cancel out naturally across all 14 poles.
              <br />
              2. <strong>Exact 0.00 dB LR4 Sub Lock:</strong> pre-lattice highs are high-passed
              so inter-stage drive never creates muddy sub-band intermodulation, while
              post-lattice Mid sums cleanly with the mono LR4 sub to an exact flat all-pass!
            </p>
          </article>
        )}
      </div>

      <div className="fg-bench" aria-label="Live 14-pole morph cube bench">
        <div className="fg-bench__bar">
          <span className="fg-bench__title">
            Lab 3 · Live 7-Stage Cytomic TPT Lattice (AudioWorklet)
          </span>
          <div className="fg-chips">
            <button
              type="button"
              className="fg-chip fg-chip--play"
              aria-pressed={activeAudioLab === "lab3"}
              onClick={() => toggleLabAudio("lab3")}
            >
              {activeAudioLab === "lab3" ? "■ Stop Lattice" : "▶ Play 14-Pole Lattice"}
            </button>
            {RECIPES.map((r) => (
              <button
                key={r.name}
                type="button"
                className="fg-chip"
                onClick={() => {
                  setCubeIndex(r.cubeIndex);
                  setMorphX(r.morphX);
                  setMorphY(r.morphY);
                  setMorphZ(r.morphZ);
                  setRes(r.res);
                  setTopology(r.topology);
                  setStagesCount(r.stages);
                  setDrive(r.drive);
                  setDrivePos(r.drivePos);
                  setAudioSource(r.source);
                }}
              >
                {r.name}
              </button>
            ))}
          </div>
        </div>

        <div className="fg-bench__body fg-bench__body--split">
          <div className="fg-bench__stage">
            <div className="fg-canvas-box fg-canvas-box--interactive">
              <span className="fg-canvas-label">
                XY Morph Pad · {FACTORY_CUBES[cubeIndex].name} (drag or use arrow keys)
              </span>
              <canvas
                ref={lab3PadRef}
                width={LAB3_PAD_W}
                height={LAB3_PAD_H}
                tabIndex={0}
                role="slider"
                aria-label={`Interactive 2D XY morph pad for ${FACTORY_CUBES[cubeIndex].name}; use arrow keys to adjust Morph X and Y`}
                aria-valuemin={0}
                aria-valuemax={100}
                aria-valuenow={Math.round(morphX * 100)}
                aria-valuetext={`X ${Math.round(morphX * 100)}%, Y ${Math.round(morphY * 100)}%`}
                onKeyDown={handlePadKeyDown}
                onPointerDown={(e) => handlePadPointer(e, true)}
                onPointerMove={(e) => handlePadPointer(e, false)}
                onPointerUp={() => {
                  padDraggingRef.current = false;
                }}
                onPointerCancel={() => {
                  padDraggingRef.current = false;
                }}
              />
            </div>

            <div className="fg-canvas-pair">
              <div className="fg-canvas-box">
                <span className="fg-canvas-label">{stagesCount * 2}-Pole Unit Circle</span>
                <canvas
                  ref={lab3ZRef}
                  width={LAB3_Z_W}
                  height={LAB3_Z_H}
                  aria-label="14-pole Z-plane unit circle visualization"
                />
              </div>
              <div className="fg-canvas-box">
                <span className="fg-canvas-label">Live Lattice Magnitude</span>
                <canvas
                  ref={lab3BodeRef}
                  width={LAB3_BODE_W}
                  height={LAB3_BODE_H}
                  aria-label="14-pole magnitude curve visualization"
                />
              </div>
            </div>
          </div>

          <div className="fg-bench__controls">
            <div>
              <span className="fg-stat__k" style={{ display: "block", marginBlockEnd: "0.35rem" }}>
                Cube Bank
              </span>
              <div className="fg-chips" role="group" aria-label="Filter cube">
                {FACTORY_CUBES.map((c, idx) => (
                  <button
                    key={c.id}
                    type="button"
                    className="fg-chip"
                    aria-pressed={cubeIndex === idx}
                    onClick={() => setCubeIndex(idx)}
                  >
                    {c.name}
                  </button>
                ))}
              </div>
            </div>

            <div
              style={{
                display: "grid",
                gap: "0.5rem",
                gridTemplateColumns: "repeat(auto-fit, minmax(11rem, 1fr))",
              }}
            >
              <div>
                <span className="fg-stat__k" style={{ display: "block", marginBlockEnd: "0.35rem" }}>
                  Topology
                </span>
                <div className="fg-chips" role="group" aria-label="Filter topology">
                  {["Serial", "Hybrid", "Parallel"].map((name, idx) => (
                    <button
                      key={name}
                      type="button"
                      className="fg-chip"
                      aria-pressed={topology === idx}
                      onClick={() => setTopology(idx)}
                    >
                      {name}
                    </button>
                  ))}
                </div>
              </div>
              <div>
                <span className="fg-stat__k" style={{ display: "block", marginBlockEnd: "0.35rem" }}>
                  Test Source
                </span>
                <div className="fg-chips" role="group" aria-label="Test audio source">
                  {[
                    ["reese", "Neuro Reese"],
                    ["chord", "Minor-9 Saw"],
                    ["drums", "172 Break"],
                  ].map(([id, label]) => (
                    <button
                      key={id}
                      type="button"
                      className="fg-chip"
                      aria-pressed={audioSource === id}
                      onClick={() => setAudioSource(id)}
                    >
                      {label}
                    </button>
                  ))}
                </div>
              </div>
            </div>

            <SliderControl
              id="l3-x"
              label="Morph X (Left ↔ Right)"
              min={0}
              max={1}
              step={0.01}
              value={morphX}
              displayValue={`${Math.round(morphX * 100)}%`}
              onChange={setMorphX}
            />
            <SliderControl
              id="l3-y"
              label="Morph Y (Top ↔ Bottom)"
              min={0}
              max={1}
              step={0.01}
              value={morphY}
              displayValue={`${Math.round(morphY * 100)}%`}
              onChange={setMorphY}
            />
            <SliderControl
              id="l3-z"
              label="Morph Z (Front ↔ Back Face)"
              min={0}
              max={1}
              step={0.01}
              value={morphZ}
              displayValue={`${Math.round(morphZ * 100)}%`}
              onChange={setMorphZ}
            />
            <SliderControl
              id="l3-res"
              label="Resonance"
              min={0}
              max={1}
              step={0.01}
              value={res}
              displayValue={`${Math.round(res * 100)}%`}
              onChange={setRes}
            />
            <SliderControl
              id="l3-drive"
              label="Saturation Drive"
              min={0}
              max={1}
              step={0.01}
              value={drive}
              displayValue={`${Math.round(drive * 100)}%`}
              onChange={setDrive}
            />

            <div className="fg-chips">
              {[
                [2, "4P"],
                [4, "8P"],
                [6, "12P"],
                [7, "14P"],
              ].map(([n, lbl]) => (
                <button
                  key={lbl}
                  type="button"
                  className="fg-chip"
                  aria-pressed={stagesCount === n}
                  onClick={() => setStagesCount(n)}
                >
                  {lbl}
                </button>
              ))}
              {["Pre", "Inter-Stage", "Post"].map((lbl, idx) => (
                <button
                  key={lbl}
                  type="button"
                  className="fg-chip"
                  aria-pressed={drivePos === idx}
                  onClick={() => setDrivePos(idx)}
                >
                  {lbl}
                </button>
              ))}
              <button
                type="button"
                className="fg-chip"
                aria-pressed={subLock}
                onClick={() => setSubLock((v) => !v)}
              >
                Sub Lock: {subLock ? "ON" : "OFF"}
              </button>
            </div>
          </div>
        </div>
      </div>

      {showEngineer && (
        <CodeExcerpt
          file="products/pole-field/engine/src/engine.cpp"
          lines="37-42"
          code={CPP_SUBLOCK}
        />
      )}
    </section>
  );
}
