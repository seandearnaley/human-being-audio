"use client";

import { useCallback, useEffect, useId, useRef, useState } from "react";
import manifest from "./audioManifest.json";

const PLAY_EVENT = "hba:showcase-play";

function formatTime(sec) {
  if (!Number.isFinite(sec) || sec < 0) return "0:00";
  const whole = Math.floor(sec);
  const mins = Math.floor(whole / 60);
  const secs = whole % 60;
  return `${mins}:${secs.toString().padStart(2, "0")}`;
}

export default function ShowcasePlayer({ slug, productName, hue }) {
  const tracks = manifest[slug] ?? [];
  const [activeIndex, setActiveIndex] = useState(0);
  const [isPlaying, setIsPlaying] = useState(false);
  const [isLoading, setIsLoading] = useState(false);
  const [currentTime, setCurrentTime] = useState(0);
  const [duration, setDuration] = useState(tracks[0]?.duration ?? 0);
  const [autoAdvance, setAutoAdvance] = useState(true);
  const [hoverProgress, setHoverProgress] = useState(null);
  const [hydrated, setHydrated] = useState(false);
  const [reducedMotion, setReducedMotion] = useState(false);

  const rootRef = useRef(null);
  const audioRef = useRef(null);
  const canvasRef = useRef(null);
  const stageRef = useRef(null);

  // Lazy Web Audio graph refs (connected only on first real playback)
  const audioCtxRef = useRef(null);
  const analyserRef = useRef(null);
  const freqDataRef = useRef(null);
  const sourceNodeRef = useRef(null);

  // Canvas animation & visibility refs
  const rafRef = useRef(0);
  const visibleRef = useRef(true);
  const playingRef = useRef(false);
  const energyRef = useRef(0);
  const hoverRef = useRef(null);
  const activeTrackRef = useRef(tracks[0]);
  const currentTimeRef = useRef(0);
  const durationRef = useRef(tracks[0]?.duration ?? 0);
  const reducedMotionRef = useRef(false);
  const pendingPlayRef = useRef(false);

  const headingId = useId();
  const activeTrack = tracks[activeIndex] ?? tracks[0];

  useEffect(() => {
    activeTrackRef.current = activeTrack;
    durationRef.current = duration || activeTrack?.duration || 1;
  }, [activeTrack, duration]);

  useEffect(() => {
    currentTimeRef.current = currentTime;
  }, [currentTime]);

  useEffect(() => {
    hoverRef.current = hoverProgress;
  }, [hoverProgress]);

  useEffect(() => {
    playingRef.current = isPlaying;
  }, [isPlaying]);

  useEffect(() => {
    setHydrated(true);
    const mq = window.matchMedia("(prefers-reduced-motion: reduce)");
    const syncMq = (matches) => {
      reducedMotionRef.current = matches;
      setReducedMotion(matches);
    };
    syncMq(mq.matches);
    const onMqChange = (e) => syncMq(e.matches);
    mq.addEventListener("change", onMqChange);
    return () => mq.removeEventListener("change", onMqChange);
  }, []);

  const ensureAnalyser = useCallback(() => {
    if (analyserRef.current) {
      if (audioCtxRef.current?.state === "suspended") {
        audioCtxRef.current.resume().catch(() => {});
      }
      return;
    }
    const audio = audioRef.current;
    if (!audio || typeof window === "undefined") return;
    const AudioCtx = window.AudioContext || window.webkitAudioContext;
    if (!AudioCtx) return;
    try {
      const ctx = new AudioCtx();
      const source = ctx.createMediaElementSource(audio);
      const analyser = ctx.createAnalyser();
      analyser.fftSize = 256;
      analyser.smoothingTimeConstant = 0.78;
      source.connect(analyser);
      analyser.connect(ctx.destination);
      audioCtxRef.current = ctx;
      sourceNodeRef.current = source;
      analyserRef.current = analyser;
      freqDataRef.current = new Uint8Array(analyser.frequencyBinCount);
    } catch {
      // Fallback gracefully to waveform progress without FFT if Web Audio is restricted
    }
  }, []);

  const drawFrame = useCallback(() => {
    const canvas = canvasRef.current;
    const track = activeTrackRef.current;
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
    const audio = audioRef.current;
    const liveDur =
      audio && Number.isFinite(audio.duration) && audio.duration > 0
        ? audio.duration
        : durationRef.current || track.duration || 1;
    const liveTime = playingRef.current && audio ? audio.currentTime : currentTimeRef.current;
    const progress = Math.max(0, Math.min(1, liveTime / liveDur));

    // Read real-time FFT spectrum when playing and motion is allowed
    let energy = 0;
    const freqData = freqDataRef.current;
    const hasLiveFft =
      playingRef.current && !reducedMotionRef.current && analyserRef.current && freqData;
    if (hasLiveFft) {
      analyserRef.current.getByteFrequencyData(freqData);
      let sum = 0;
      const bins = Math.min(48, freqData.length);
      for (let i = 0; i < bins; i++) sum += freqData[i];
      energy = sum / (bins * 255);
    }
    energyRef.current += (energy - energyRef.current) * 0.16;
    const smoothEnergy = energyRef.current;

    if (rootRef.current && !reducedMotionRef.current) {
      rootRef.current.style.setProperty("--energy", smoothEnergy.toFixed(3));
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

    // Draw the 64 studio peak bars with mirror reflection
    const step = usableW / barCount;
    const barW = Math.max(1.8, Math.min(4.5, step * 0.62));
    const hover = hoverRef.current;

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

      // Upper peak bar
      ctx.beginPath();
      if (ctx.roundRect) {
        ctx.roundRect(x, centerY - hUp, barW, hUp - 1, [barW * 0.5, barW * 0.5, 1, 1]);
      } else {
        ctx.rect(x, centerY - hUp, barW, hUp - 1);
      }
      ctx.fill();

      // Mirrored lower reflection bar
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
    if (hover !== null && !reducedMotionRef.current) {
      const hx = padX + hover * usableW;
      ctx.strokeStyle = "rgba(255, 255, 255, 0.32)";
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(hx, 6);
      ctx.lineTo(hx, height - 6);
      ctx.stroke();
    }

    // Active playhead hairline + luminous bead
    if (progress > 0 || playingRef.current) {
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
  }, [hue]);

  const stopLoop = useCallback(() => {
    if (rafRef.current) {
      cancelAnimationFrame(rafRef.current);
      rafRef.current = 0;
    }
  }, []);

  const startLoop = useCallback(() => {
    stopLoop();
    if (!visibleRef.current || document.hidden) return;
    const tick = () => {
      drawFrame();
      if (playingRef.current && !reducedMotionRef.current && visibleRef.current && !document.hidden) {
        rafRef.current = requestAnimationFrame(tick);
      } else {
        rafRef.current = 0;
      }
    };
    rafRef.current = requestAnimationFrame(tick);
  }, [drawFrame, stopLoop]);

  // Redraw static frame on track switch, seek, hover, or resize
  useEffect(() => {
    drawFrame();
  }, [activeIndex, currentTime, duration, hoverProgress, isPlaying, reducedMotion, drawFrame]);

  useEffect(() => {
    const stage = stageRef.current;
    if (!stage || typeof ResizeObserver === "undefined") return;
    const ro = new ResizeObserver(() => drawFrame());
    ro.observe(stage);
    return () => ro.disconnect();
  }, [drawFrame]);

  // Efficient background processing: pause canvas RAF when offscreen via
  // contentvisibilityautostatechange (direct listener; does not bubble) + IO fallback.
  useEffect(() => {
    const root = rootRef.current;
    if (!root) return;

    const handleVisibility = (isVisible) => {
      visibleRef.current = isVisible;
      if (!isVisible) {
        stopLoop();
      } else if (playingRef.current && !reducedMotionRef.current) {
        startLoop();
      } else {
        drawFrame();
      }
    };

    const onContentVisibility = (event) => {
      handleVisibility(!event.skipped);
    };
    root.addEventListener("contentvisibilityautostatechange", onContentVisibility);

    let observer = null;
    const supportsCV = "contentVisibility" in document.documentElement.style;
    if (!supportsCV && typeof IntersectionObserver !== "undefined") {
      observer = new IntersectionObserver(
        (entries) => {
          for (const entry of entries) {
            handleVisibility(entry.isIntersecting);
          }
        },
        { rootMargin: "200px" },
      );
      observer.observe(root);
    }

    const onDocVis = () => {
      if (document.hidden) stopLoop();
      else if (playingRef.current && !reducedMotionRef.current && visibleRef.current) startLoop();
    };
    document.addEventListener("visibilitychange", onDocVis);

    return () => {
      root.removeEventListener("contentvisibilityautostatechange", onContentVisibility);
      observer?.disconnect();
      document.removeEventListener("visibilitychange", onDocVis);
      stopLoop();
    };
  }, [drawFrame, startLoop, stopLoop]);

  // Exclusive global playback: starting another product's player pauses this one
  useEffect(() => {
    const onOtherPlay = (event) => {
      if (event.detail?.slug !== slug && audioRef.current && !audioRef.current.paused) {
        audioRef.current.pause();
      }
    };
    window.addEventListener(PLAY_EVENT, onOtherPlay);
    return () => window.removeEventListener(PLAY_EVENT, onOtherPlay);
  }, [slug]);

  const startPlayback = useCallback(async () => {
    const audio = audioRef.current;
    if (!audio) return;
    window.dispatchEvent(new CustomEvent(PLAY_EVENT, { detail: { slug } }));
    ensureAnalyser();
    setIsLoading(true);
    try {
      await audio.play();
    } catch {
      setIsLoading(false);
    }
  }, [ensureAnalyser, slug]);

  const togglePlay = useCallback(() => {
    const audio = audioRef.current;
    if (!audio) return;
    if (audio.paused) {
      startPlayback();
    } else {
      audio.pause();
    }
  }, [startPlayback]);

  const selectTrack = useCallback(
    (index, shouldPlay) => {
      const bounded = ((index % tracks.length) + tracks.length) % tracks.length;
      const audio = audioRef.current;
      if (bounded === activeIndex) {
        if (shouldPlay) togglePlay();
        return;
      }
      pendingPlayRef.current = Boolean(shouldPlay);
      setActiveIndex(bounded);
      setCurrentTime(0);
      setDuration(tracks[bounded]?.duration ?? 0);
      if (audio) {
        audio.currentTime = 0;
        if (!shouldPlay) audio.pause();
      }
    },
    [activeIndex, togglePlay, tracks],
  );

  // Trigger playback cleanly after activeIndex swaps src
  useEffect(() => {
    if (!pendingPlayRef.current) return;
    pendingPlayRef.current = false;
    const audio = audioRef.current;
    if (!audio) return;
    audio.load();
    startPlayback();
  }, [activeIndex, startPlayback]);

  // Wire Media Session metadata & hardware media keys when active
  const syncMediaSession = useCallback(() => {
    if (typeof navigator === "undefined" || !("mediaSession" in navigator) || !activeTrack) return;
    try {
      navigator.mediaSession.metadata = new MediaMetadata({
        title: activeTrack.title,
        artist: "Human Being Audio",
        album: productName,
      });
      navigator.mediaSession.setActionHandler("play", () => startPlayback());
      navigator.mediaSession.setActionHandler("pause", () => audioRef.current?.pause());
      navigator.mediaSession.setActionHandler("previoustrack", () =>
        selectTrack(activeIndex - 1, true),
      );
      navigator.mediaSession.setActionHandler("nexttrack", () =>
        selectTrack(activeIndex + 1, true),
      );
    } catch {
      // Ignore if MediaSession actions are unsupported
    }
  }, [activeIndex, activeTrack, productName, selectTrack, startPlayback]);

  // Wire native <audio> events
  useEffect(() => {
    const audio = audioRef.current;
    if (!audio) return;

    const onPlay = () => {
      playingRef.current = true;
      setIsPlaying(true);
      setIsLoading(false);
      window.dispatchEvent(new CustomEvent(PLAY_EVENT, { detail: { slug } }));
      syncMediaSession();
      if (!reducedMotionRef.current) startLoop();
    };
    const onPause = () => {
      playingRef.current = false;
      setIsPlaying(false);
      setIsLoading(false);
      energyRef.current = 0;
      if (rootRef.current) rootRef.current.style.setProperty("--energy", "0");
      stopLoop();
      drawFrame();
    };
    const onTimeUpdate = () => {
      setCurrentTime(audio.currentTime);
    };
    const onLoadedMetadata = () => {
      if (Number.isFinite(audio.duration) && audio.duration > 0) {
        setDuration(audio.duration);
      }
    };
    const onWaiting = () => setIsLoading(true);
    const onCanPlay = () => setIsLoading(false);
    const onEnded = () => {
      if (autoAdvance && tracks.length > 1) {
        selectTrack(activeIndex + 1, true);
      } else {
        setIsPlaying(false);
        setCurrentTime(0);
      }
    };

    audio.addEventListener("play", onPlay);
    audio.addEventListener("pause", onPause);
    audio.addEventListener("timeupdate", onTimeUpdate);
    audio.addEventListener("loadedmetadata", onLoadedMetadata);
    audio.addEventListener("waiting", onWaiting);
    audio.addEventListener("canplay", onCanPlay);
    audio.addEventListener("ended", onEnded);
    return () => {
      audio.removeEventListener("play", onPlay);
      audio.removeEventListener("pause", onPause);
      audio.removeEventListener("timeupdate", onTimeUpdate);
      audio.removeEventListener("loadedmetadata", onLoadedMetadata);
      audio.removeEventListener("waiting", onWaiting);
      audio.removeEventListener("canplay", onCanPlay);
      audio.removeEventListener("ended", onEnded);
    };
  }, [activeIndex, autoAdvance, drawFrame, selectTrack, slug, startLoop, stopLoop, syncMediaSession, tracks.length]);

  const handleScrub = (event) => {
    const next = parseFloat(event.target.value);
    setCurrentTime(next);
    if (audioRef.current) {
      audioRef.current.currentTime = next;
    }
  };

  const handlePointerMove = (event) => {
    const rect = event.currentTarget.getBoundingClientRect();
    if (rect.width <= 0) return;
    const u = Math.max(0, Math.min(1, (event.clientX - rect.left) / rect.width));
    setHoverProgress(u);
  };

  if (!activeTrack) return null;

  const effectiveDur = duration || activeTrack.duration || 1;

  return (
    <figure
      ref={rootRef}
      className="showcase"
      data-showcase={slug}
      data-playing={isPlaying ? "true" : "false"}
      data-ready={hydrated ? "true" : "false"}
      aria-labelledby={headingId}
    >
      <figcaption id={headingId} className="showcase__kicker">
        <span className="showcase__kicker-label">
          <span className="showcase__led" aria-hidden="true" />
          Hear {productName}
        </span>
        <span className="mono showcase__count">
          {String(activeIndex + 1).padStart(2, "0")} / {String(tracks.length).padStart(2, "0")} clips
        </span>
      </figcaption>

      {/* Native <audio controls> stays visible without JS; hidden visually once custom deck hydrates */}
      <div className="showcase__native">
        <audio
          ref={audioRef}
          controls
          preload="none"
          aria-label={`${productName} audio example`}
          src={activeTrack.src}
        />
      </div>

      {/* Interactive animated studio deck */}
      <div className="showcase__deck">
        <div className="showcase__ambient" aria-hidden="true" />

        <div className="showcase__top">
          <button
            type="button"
            className="showcase__play"
            onClick={togglePlay}
            aria-label={
              isPlaying
                ? `Pause ${activeTrack.title}`
                : `Play ${activeTrack.title}`
            }
            data-loading={isLoading ? "true" : "false"}
          >
            {isPlaying ? (
              <svg viewBox="0 0 20 20" aria-hidden="true" className="showcase__icon">
                <rect x="4.5" y="3.5" width="3.8" height="13" rx="1.2" />
                <rect x="11.7" y="3.5" width="3.8" height="13" rx="1.2" />
              </svg>
            ) : (
              <svg viewBox="0 0 20 20" aria-hidden="true" className="showcase__icon showcase__icon--play">
                <path d="M5.5 3.6c0-.8.9-1.3 1.6-.9l9.5 6.4c.6.4.6 1.4 0 1.8l-9.5 6.4c-.7.4-1.6-.1-1.6-.9V3.6z" />
              </svg>
            )}
          </button>

          <div className="showcase__now">
            <div className="showcase__title-row">
              <span className="showcase__title">{activeTrack.title}</span>
              <span className="showcase__badge">{activeTrack.tag}</span>
            </div>
            <div className="showcase__meta mono">
              <span>{Math.round(activeTrack.bpm)} BPM</span>
              <span aria-hidden="true">·</span>
              <span>{activeTrack.key}</span>
            </div>
          </div>

          <div className="showcase__transport">
            <button
              type="button"
              className="showcase__step"
              onClick={() => selectTrack(activeIndex - 1, isPlaying)}
              aria-label={`Previous ${productName} clip`}
            >
              <svg viewBox="0 0 16 16" aria-hidden="true">
                <rect x="2.5" y="3" width="1.8" height="10" rx="0.6" />
                <path d="M12.5 3.4 6 8l6.5 4.6V3.4z" />
              </svg>
            </button>
            <button
              type="button"
              className="showcase__step"
              onClick={() => selectTrack(activeIndex + 1, isPlaying)}
              aria-label={`Next ${productName} clip`}
            >
              <svg viewBox="0 0 16 16" aria-hidden="true">
                <path d="M3.5 3.4 10 8l-6.5 4.6V3.4z" />
                <rect x="11.7" y="3" width="1.8" height="10" rx="0.6" />
              </svg>
            </button>
            <button
              type="button"
              className="showcase__auto"
              aria-pressed={autoAdvance ? "true" : "false"}
              onClick={() => setAutoAdvance((v) => !v)}
              title={autoAdvance ? "Auto-advance on" : "Repeat single clip"}
            >
              {autoAdvance ? "Auto" : "1×"}
            </button>
            <span className="showcase__time mono" aria-live="off">
              {formatTime(currentTime)} / {formatTime(effectiveDur)}
            </span>
          </div>
        </div>

        <div
          ref={stageRef}
          className="showcase__stage"
          onPointerMove={handlePointerMove}
          onPointerLeave={() => setHoverProgress(null)}
        >
          <canvas ref={canvasRef} className="showcase__canvas" aria-hidden="true" />
          <input
            type="range"
            className="showcase__scrub"
            min={0}
            max={effectiveDur}
            step={0.05}
            value={Math.min(currentTime, effectiveDur)}
            onChange={handleScrub}
            aria-label={`Seek ${activeTrack.title}`}
            aria-valuetext={`${formatTime(currentTime)} of ${formatTime(effectiveDur)}`}
          />
        </div>

        <p className="showcase__note">{activeTrack.note}</p>

        <ul className="showcase__tracks" role="list" aria-label={`${productName} audio clips`}>
          {tracks.map((track, idx) => {
            const isSelected = idx === activeIndex;
            const isTrackPlaying = isSelected && isPlaying;
            return (
              <li key={track.id}>
                <button
                  type="button"
                  className="showcase__track"
                  aria-current={isSelected ? "true" : undefined}
                  data-playing={isTrackPlaying ? "true" : "false"}
                  onClick={() => selectTrack(idx, true)}
                >
                  <span className="showcase__track-idx mono" aria-hidden="true">
                    {isTrackPlaying ? (
                      <span className="showcase__eq">
                        <i />
                        <i />
                        <i />
                      </span>
                    ) : (
                      String(idx + 1).padStart(2, "0")
                    )}
                  </span>
                  <span className="showcase__track-main">
                    <span className="showcase__track-title">{track.title}</span>
                    <span className="showcase__track-sub mono">
                      {track.tag} · {Math.round(track.bpm)} BPM
                    </span>
                  </span>
                  <span className="showcase__track-dur mono">
                    {formatTime(track.duration)}
                  </span>
                </button>
              </li>
            );
          })}
        </ul>
      </div>
    </figure>
  );
}
