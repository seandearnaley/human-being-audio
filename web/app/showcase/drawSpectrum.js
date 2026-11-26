// DPR-aware canvas waveform + live Web Audio FFT spectrum renderer for ShowcasePlayer.

export function drawShowcaseFrame({
  canvas,
  root,
  audio,
  track,
  hue,
  isPlaying,
  reducedMotion,
  analyser,
  freqData,
  energyRef,
  hover,
  currentTime,
  duration,
}) {
  if (!canvas || !track) return;

  const dpr = Math.min(window.devicePixelRatio || 1, 2);
  const width = canvas.clientWidth;
  const height = canvas.clientHeight;
  if (width <= 0 || height <= 0) return;

  const targetW = Math.round(width * dpr);
  const targetH = Math.round(height * dpr);
  if (canvas.width !== targetW || canvas.height !== targetH) {
    canvas.width = targetW;
    canvas.height = targetH;
  }

  const ctx = canvas.getContext("2d");
  if (!ctx) return;

  ctx.save();
  ctx.scale(dpr, dpr);
  ctx.clearRect(0, 0, width, height);

  const peaks = track.peaks ?? [];
  const barCount = peaks.length || 64;
  const liveDur =
    audio && Number.isFinite(audio.duration) && audio.duration > 0
      ? audio.duration
      : duration || track.duration || 1;
  const liveTime = isPlaying && audio ? audio.currentTime : currentTime;
  const progress = Math.max(0, Math.min(1, liveTime / liveDur));

  let energy = 0;
  const hasLiveFft = isPlaying && !reducedMotion && analyser && freqData;
  if (hasLiveFft) {
    analyser.getByteFrequencyData(freqData);
    let sum = 0;
    const bins = Math.min(48, freqData.length);
    for (let i = 0; i < bins; i++) sum += freqData[i];
    energy = sum / (bins * 255);
  }
  energyRef.current += (energy - energyRef.current) * 0.16;
  const smoothEnergy = energyRef.current;

  if (root && !reducedMotion) {
    root.style.setProperty("--energy", smoothEnergy.toFixed(3));
  }

  const padX = 10;
  const usableW = Math.max(1, width - padX * 2);
  const centerY = height * 0.56;
  const maxUp = height * 0.44;
  const maxDown = height * 0.28;

  // 4-bar musical grid hairlines (25%, 50%, 75%)
  ctx.strokeStyle = "rgba(255, 255, 255, 0.065)";
  ctx.lineWidth = 1;
  for (let q = 1; q <= 3; q++) {
    const gx = Math.round(padX + usableW * (q / 4)) + 0.5;
    ctx.beginPath();
    ctx.moveTo(gx, 8);
    ctx.lineTo(gx, height - 8);
    ctx.stroke();
  }

  // Center horizon axis
  ctx.strokeStyle = "rgba(255, 255, 255, 0.09)";
  ctx.beginPath();
  ctx.moveTo(padX, Math.round(centerY) + 0.5);
  ctx.lineTo(width - padX, Math.round(centerY) + 0.5);
  ctx.stroke();

  // Soft live spectrum ribbon behind the waveform when playing
  if (hasLiveFft && smoothEnergy > 0.01) {
    const grad = ctx.createLinearGradient(padX, 0, padX + usableW, 0);
    grad.addColorStop(0, `oklch(0.75 0.16 ${hue} / 0.06)`);
    grad.addColorStop(
      Math.max(0.05, Math.min(0.95, progress)),
      `oklch(0.80 0.17 ${hue} / 0.30)`,
    );
    grad.addColorStop(1, `oklch(0.75 0.16 ${hue} / 0.04)`);
    ctx.fillStyle = grad;
    ctx.beginPath();
    ctx.moveTo(padX, centerY);
    const pts = 32;
    for (let i = 0; i <= pts; i++) {
      const u = i / pts;
      const bin = Math.min(freqData.length - 1, Math.floor(Math.pow(u, 1.35) * 44));
      const amp = (freqData[bin] / 255) * maxUp * 0.85;
      ctx.lineTo(padX + u * usableW, centerY - amp);
    }
    ctx.lineTo(padX + usableW, centerY);
    ctx.closePath();
    ctx.fill();
  }

  // 64 studio peak bars with mirror reflection
  const step = usableW / barCount;
  const barW = Math.max(1.8, Math.min(4.5, step * 0.62));

  for (let i = 0; i < barCount; i++) {
    const normPeak = Math.max(0.08, (peaks[i] ?? 12) / 100);
    const frac = (i + 0.5) / barCount;
    const x = padX + i * step + (step - barW) * 0.5;
    const played = frac <= progress;
    const inHover = hover !== null && frac <= hover && !played;

    let lift = 1;
    if (hasLiveFft) {
      const dist = Math.abs(frac - progress);
      const bin = Math.min(freqData.length - 1, Math.floor((i / barCount) * 46));
      const specBoost = (freqData[bin] / 255) * 0.26;
      const localPulse = Math.exp(-dist * 11) * smoothEnergy * 0.34;
      lift = 1 + specBoost + localPulse;
    }

    const hUp = Math.min(maxUp, Math.max(2.5, normPeak * maxUp * lift));
    const hDown = Math.min(maxDown, Math.max(1.5, normPeak * maxDown * (0.85 + 0.2 * lift)));

    if (played) {
      ctx.fillStyle = `oklch(0.79 0.16 ${hue})`;
    } else if (inHover) {
      ctx.fillStyle = `oklch(0.75 0.15 ${hue} / 0.45)`;
    } else {
      ctx.fillStyle = "rgba(232, 234, 246, 0.28)";
    }

    ctx.beginPath();
    if (ctx.roundRect) {
      ctx.roundRect(x, centerY - hUp, barW, hUp - 1, [barW * 0.5, barW * 0.5, 1, 1]);
    } else {
      ctx.rect(x, centerY - hUp, barW, hUp - 1);
    }
    ctx.fill();

    ctx.fillStyle = played
      ? `oklch(0.75 0.16 ${hue} / 0.36)`
      : "rgba(232, 234, 246, 0.11)";
    ctx.beginPath();
    if (ctx.roundRect) {
      ctx.roundRect(x, centerY + 1.5, barW, hDown, [1, 1, barW * 0.5, barW * 0.5]);
    } else {
      ctx.rect(x, centerY + 1.5, barW, hDown);
    }
    ctx.fill();
  }

  // Hover seek ghost hairline
  if (hover !== null && !reducedMotion) {
    const hx = padX + hover * usableW;
    ctx.strokeStyle = "rgba(255, 255, 255, 0.32)";
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(hx, 6);
    ctx.lineTo(hx, height - 6);
    ctx.stroke();
  }

  // Active playhead hairline + luminous bead
  if (progress > 0 || isPlaying) {
    const px = padX + progress * usableW;
    ctx.strokeStyle = `oklch(0.88 0.14 ${hue})`;
    ctx.lineWidth = 1.75;
    ctx.beginPath();
    ctx.moveTo(px, 6);
    ctx.lineTo(px, height - 6);
    ctx.stroke();

    ctx.fillStyle = "#ffffff";
    ctx.beginPath();
    ctx.arc(px, centerY, 3 + smoothEnergy * 1.6, 0, Math.PI * 2);
    ctx.fill();
  }

  ctx.restore();
}
