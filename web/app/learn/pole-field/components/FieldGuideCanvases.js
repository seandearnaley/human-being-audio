import {
  FACTORY_CUBES,
  SAMPLE_RATE,
  compareCartesianVsPolar,
  stageZPoint,
} from "../dspMath";

/**
 * Scales a canvas backing store to devicePixelRatio (capped at 2x for crisp
 * Retina rendering without excessive GPU fill), resets the transform to logical
 * CSS units, and clears the frame.
 */
export function prepareHiDpiCanvas(canvas, logicalW, logicalH) {
  if (!canvas) return null;
  const dpr = Math.min(typeof window !== "undefined" ? window.devicePixelRatio || 1 : 1, 2);
  const targetW = Math.round(logicalW * dpr);
  const targetH = Math.round(logicalH * dpr);
  if (canvas.width !== targetW || canvas.height !== targetH) {
    canvas.width = targetW;
    canvas.height = targetH;
  }
  const ctx = canvas.getContext("2d");
  if (!ctx) return null;
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, logicalW, logicalH);
  return ctx;
}

/** Draws a Bode frequency response (25 Hz - 18 kHz, -24..+24 dB) with optional comparison curve. */
export function drawBodeCanvas(
  canvas,
  logicalW,
  logicalH,
  evalDbAtHz,
  accentHex = "#e879c5",
  secondaryEval = null,
) {
  const ctx = prepareHiDpiCanvas(canvas, logicalW, logicalH);
  if (!ctx) return;

  const W = logicalW;
  const H = logicalH;
  const padL = 38;
  const padR = 14;
  const padT = 26;
  const padB = 24;
  const plotW = W - padL - padR;
  const plotH = H - padT - padB;

  const minLog = Math.log10(25);
  const maxLog = Math.log10(18000);
  const xOfHz = (hz) => padL + ((Math.log10(hz) - minLog) / (maxLog - minLog)) * plotW;
  const yOfDb = (db) => padT + ((24 - Math.max(-24, Math.min(24, db))) / 48) * plotH;

  // Grid
  ctx.strokeStyle = "rgba(255,255,255,0.08)";
  ctx.lineWidth = 1;
  for (const hz of [50, 100, 200, 500, 1000, 2000, 5000, 10000]) {
    const x = xOfHz(hz);
    ctx.beginPath();
    ctx.moveTo(x, padT);
    ctx.lineTo(x, padT + plotH);
    ctx.stroke();
    ctx.fillStyle = "rgba(205,212,232,0.52)";
    ctx.font = '10px "Martian Mono", monospace';
    ctx.textAlign = "center";
    ctx.fillText(hz >= 1000 ? `${hz / 1000}k` : `${hz}`, x, H - 7);
  }
  for (const db of [-18, -12, 0, 12, 18]) {
    const y = yOfDb(db);
    ctx.strokeStyle = db === 0 ? "rgba(255,255,255,0.22)" : "rgba(255,255,255,0.07)";
    ctx.beginPath();
    ctx.moveTo(padL, y);
    ctx.lineTo(padL + plotW, y);
    ctx.stroke();
    ctx.fillStyle = "rgba(205,212,232,0.52)";
    ctx.font = '9.5px "Martian Mono", monospace';
    ctx.textAlign = "right";
    ctx.fillText(db > 0 ? `+${db}` : `${db}`, padL - 6, y + 3);
  }

  // Optional comparison curve (e.g. Naive Cartesian)
  if (secondaryEval) {
    ctx.beginPath();
    for (let i = 0; i <= 160; i++) {
      const hz = Math.pow(10, minLog + (i / 160) * (maxLog - minLog));
      const x = padL + (i / 160) * plotW;
      const y = yOfDb(secondaryEval(hz));
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    }
    ctx.strokeStyle = "rgba(255, 118, 118, 0.75)";
    ctx.lineWidth = 1.8;
    ctx.setLineDash([5, 4]);
    ctx.stroke();
    ctx.setLineDash([]);
  }

  // Primary curve + soft fill
  ctx.beginPath();
  for (let i = 0; i <= 180; i++) {
    const hz = Math.pow(10, minLog + (i / 180) * (maxLog - minLog));
    const x = padL + (i / 180) * plotW;
    const y = yOfDb(evalDbAtHz(hz));
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  }
  ctx.strokeStyle = accentHex;
  ctx.lineWidth = 2.4;
  ctx.stroke();

  const zeroY = yOfDb(0);
  ctx.lineTo(padL + plotW, zeroY);
  ctx.lineTo(padL, zeroY);
  ctx.closePath();
  ctx.fillStyle = "rgba(232, 121, 197, 0.14)";
  ctx.fill();
}

/** Draws a Z-plane unit circle with conjugate poles (X) and zeros (O). */
export function drawZPlaneCanvas(
  canvas,
  logicalW,
  logicalH,
  stages,
  activeStages = 7,
  highlightIdx = -1,
) {
  const ctx = prepareHiDpiCanvas(canvas, logicalW, logicalH);
  if (!ctx) return;

  const W = logicalW;
  const H = logicalH;
  const cx = W * 0.5;
  const cy = H * 0.56;
  const R = Math.min(W * 0.32, H * 0.35);

  // Stability interior glow
  const grad = ctx.createRadialGradient(cx, cy, R * 0.1, cx, cy, R);
  grad.addColorStop(0, "rgba(56, 217, 169, 0.04)");
  grad.addColorStop(0.82, "rgba(232, 121, 197, 0.08)");
  grad.addColorStop(1, "rgba(255, 107, 107, 0.16)");
  ctx.fillStyle = grad;
  ctx.beginPath();
  ctx.arc(cx, cy, R, 0, Math.PI * 2);
  ctx.fill();

  // Concentric reference rings + axes
  ctx.strokeStyle = "rgba(255,255,255,0.10)";
  ctx.lineWidth = 1;
  for (const frac of [0.35, 0.7]) {
    ctx.beginPath();
    ctx.arc(cx, cy, R * frac, 0, Math.PI * 2);
    ctx.stroke();
  }
  ctx.beginPath();
  ctx.moveTo(cx - R - 10, cy);
  ctx.lineTo(cx + R + 10, cy);
  ctx.moveTo(cx, cy - R - 10);
  ctx.lineTo(cx, cy + R + 10);
  ctx.stroke();

  // Unit circle |z| = 1 boundary
  ctx.strokeStyle = "rgba(255,255,255,0.42)";
  ctx.lineWidth = 1.6;
  ctx.beginPath();
  ctx.arc(cx, cy, R, 0, Math.PI * 2);
  ctx.stroke();

  // Frequency landmarks
  ctx.fillStyle = "rgba(205,212,232,0.52)";
  ctx.font = '9.5px "Martian Mono", monospace';
  ctx.textAlign = "left";
  ctx.fillText("0 Hz", cx + R + 5, cy + 3);
  ctx.textAlign = "right";
  ctx.fillText("Nyq", cx - R - 5, cy + 3);
  ctx.textAlign = "center";
  ctx.fillText("fs/4", cx, cy - R - 5);

  const count = Math.min(activeStages, stages.length);
  for (let i = 0; i < count; i++) {
    const pt = stageZPoint(stages[i], SAMPLE_RATE);
    const isHi = i === highlightIdx;
    for (const sign of [1, -1]) {
      const px = cx + pt.poleR * R * Math.cos(pt.poleTheta);
      const py = cy - sign * pt.poleR * R * Math.sin(pt.poleTheta);
      const zx = cx + pt.zeroR * R * Math.cos(pt.zeroTheta);
      const zy = cy - sign * pt.zeroR * R * Math.sin(pt.zeroTheta);

      ctx.strokeStyle = sign > 0 ? "rgba(232,121,197,0.34)" : "rgba(232,121,197,0.14)";
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(px, py);
      ctx.lineTo(zx, zy);
      ctx.stroke();

      ctx.strokeStyle = sign > 0 ? "#66d9e8" : "rgba(102,217,232,0.4)";
      ctx.lineWidth = isHi ? 2.4 : 1.7;
      ctx.beginPath();
      ctx.arc(zx, zy, isHi ? 6.5 : 4.8, 0, Math.PI * 2);
      ctx.stroke();

      const s = isHi ? 6.5 : 4.8;
      ctx.strokeStyle = sign > 0 ? "#f783ac" : "rgba(247,131,172,0.45)";
      ctx.lineWidth = isHi ? 2.6 : 1.9;
      ctx.beginPath();
      ctx.moveTo(px - s, py - s);
      ctx.lineTo(px + s, py + s);
      ctx.moveTo(px + s, py - s);
      ctx.lineTo(px - s, py + s);
      ctx.stroke();
    }
  }
}

/** Draws the Lab 2 Log-Polar arc vs Direct-Form Cartesian chord trajectory. */
export function drawLab2TrajectoryCanvas(canvas, logicalW, logicalH, lab2Data) {
  const ctx = prepareHiDpiCanvas(canvas, logicalW, logicalH);
  if (!ctx) return;

  const W = logicalW;
  const H = logicalH;
  const cx = W * 0.5;
  const cy = H * 0.53;
  const R = Math.min(W, H) * 0.39;

  ctx.strokeStyle = "rgba(255,255,255,0.38)";
  ctx.lineWidth = 1.5;
  ctx.beginPath();
  ctx.arc(cx, cy, R, 0, Math.PI * 2);
  ctx.stroke();

  ctx.strokeStyle = "rgba(255,255,255,0.1)";
  ctx.beginPath();
  ctx.moveTo(cx - R, cy);
  ctx.lineTo(cx + R, cy);
  ctx.moveTo(cx, cy - R);
  ctx.lineTo(cx, cy + R);
  ctx.stroke();

  ctx.beginPath();
  for (let i = 0; i <= 60; i++) {
    const d = compareCartesianVsPolar(i / 60).polar;
    const x = cx + d.r * R * Math.cos(d.theta);
    const y = cy - d.r * R * Math.sin(d.theta);
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  }
  ctx.strokeStyle = "#38d9a9";
  ctx.lineWidth = 2.4;
  ctx.stroke();

  ctx.beginPath();
  for (let i = 0; i <= 60; i++) {
    const d = compareCartesianVsPolar(i / 60).cartesian;
    const x = cx + d.r * R * Math.cos(d.theta);
    const y = cy - d.r * R * Math.sin(d.theta);
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  }
  ctx.strokeStyle = "#ff6b6b";
  ctx.lineWidth = 2;
  ctx.setLineDash([5, 4]);
  ctx.stroke();
  ctx.setLineDash([]);

  const drawDot = (pt, color, label) => {
    const x = cx + pt.r * R * Math.cos(pt.theta);
    const y = cy - pt.r * R * Math.sin(pt.theta);
    ctx.fillStyle = color;
    ctx.beginPath();
    ctx.arc(x, y, 6, 0, Math.PI * 2);
    ctx.fill();
    ctx.fillStyle = "#fff";
    ctx.font = '10px "Martian Mono", monospace';
    ctx.fillText(label, x + 9, y + 3);
  };
  drawDot(lab2Data.polar, "#38d9a9", "Log-Polar");
  drawDot(lab2Data.cartesian, "#ff6b6b", "Cartesian");
}

/** Draws the Lab 3 2D XY Morph Pad with corner labels and glowing puck. */
export function drawLab3MorphPadCanvas(canvas, logicalW, logicalH, cubeIndex, morphX, morphY, morphZ) {
  const ctx = prepareHiDpiCanvas(canvas, logicalW, logicalH);
  if (!ctx) return;

  const W = logicalW;
  const H = logicalH;
  const cube = FACTORY_CUBES[cubeIndex];

  ctx.strokeStyle = "rgba(255,255,255,0.08)";
  ctx.lineWidth = 1;
  for (let i = 1; i < 4; i++) {
    ctx.beginPath();
    ctx.moveTo((W * i) / 4, 0);
    ctx.lineTo((W * i) / 4, H);
    ctx.moveTo(0, (H * i) / 4);
    ctx.lineTo(W, (H * i) / 4);
    ctx.stroke();
  }

  const faceOffset = morphZ >= 0.5 ? 4 : 0;
  ctx.fillStyle = "rgba(220,226,245,0.78)";
  ctx.font = '10.5px "Martian Mono", monospace';
  ctx.textAlign = "left";
  ctx.fillText(cube.cornerNames[faceOffset + 0], 10, 38);
  ctx.fillText(cube.cornerNames[faceOffset + 2], 10, H - 10);
  ctx.textAlign = "right";
  ctx.fillText(cube.cornerNames[faceOffset + 1], W - 10, 38);
  ctx.fillText(cube.cornerNames[faceOffset + 3], W - 10, H - 10);

  const px = 14 + morphX * (W - 28);
  const py = 44 + morphY * (H - 64);
  ctx.strokeStyle = "rgba(232,121,197,0.35)";
  ctx.beginPath();
  ctx.moveTo(px, 0);
  ctx.lineTo(px, H);
  ctx.moveTo(0, py);
  ctx.lineTo(W, py);
  ctx.stroke();

  const glow = ctx.createRadialGradient(px, py, 2, px, py, 28);
  glow.addColorStop(0, "rgba(232,121,197,0.75)");
  glow.addColorStop(1, "rgba(232,121,197,0)");
  ctx.fillStyle = glow;
  ctx.beginPath();
  ctx.arc(px, py, 28, 0, Math.PI * 2);
  ctx.fill();

  ctx.fillStyle = "#ffffff";
  ctx.strokeStyle = "#e879c5";
  ctx.lineWidth = 2.5;
  ctx.beginPath();
  ctx.arc(px, py, 7, 0, Math.PI * 2);
  ctx.fill();
  ctx.stroke();
}

/** Draws the Lab 4 ADAA2 vs Naive aliasing FFT spectrum (0 - 24 kHz, -96..0 dB). */
export function drawLab4SpectrumCanvas(canvas, logicalW, logicalH, lab4Data) {
  const ctx = prepareHiDpiCanvas(canvas, logicalW, logicalH);
  if (!ctx) return;

  const W = logicalW;
  const H = logicalH;
  const padL = 40;
  const padR = 14;
  const padT = 26;
  const padB = 24;
  const plotW = W - padL - padR;
  const plotH = H - padT - padB;
  const bins = lab4Data.naive.length;
  const yOfDb = (db) => padT + ((0 - Math.max(-96, Math.min(0, db))) / 96) * plotH;

  ctx.strokeStyle = "rgba(255,255,255,0.08)";
  ctx.lineWidth = 1;
  for (const db of [0, -24, -48, -72, -96]) {
    const y = yOfDb(db);
    ctx.beginPath();
    ctx.moveTo(padL, y);
    ctx.lineTo(padL + plotW, y);
    ctx.stroke();
    ctx.fillStyle = "rgba(205,212,232,0.55)";
    ctx.font = '9.5px "Martian Mono", monospace';
    ctx.textAlign = "right";
    ctx.fillText(`${db}`, padL - 6, y + 3);
  }
  for (const khz of [2, 6, 10, 14, 18, 22]) {
    const x = padL + ((khz * 1000) / 24000) * plotW;
    ctx.beginPath();
    ctx.moveTo(x, padT);
    ctx.lineTo(x, padT + plotH);
    ctx.stroke();
    ctx.fillStyle = "rgba(205,212,232,0.55)";
    ctx.textAlign = "center";
    ctx.fillText(`${khz}k`, x, H - 7);
  }

  ctx.beginPath();
  for (let i = 1; i < bins; i++) {
    const x = padL + (i / bins) * plotW;
    const y = yOfDb(lab4Data.naive[i]);
    if (i === 1) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  }
  ctx.strokeStyle = "rgba(255, 107, 107, 0.85)";
  ctx.lineWidth = 1.6;
  ctx.stroke();

  ctx.beginPath();
  for (let i = 1; i < bins; i++) {
    const x = padL + (i / bins) * plotW;
    const y = yOfDb(lab4Data.adaa[i]);
    if (i === 1) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  }
  ctx.strokeStyle = "#38d9a9";
  ctx.lineWidth = 2.0;
  ctx.stroke();
}
