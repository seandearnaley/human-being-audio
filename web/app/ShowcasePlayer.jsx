"use client";

import { useCallback, useEffect, useId, useRef, useState } from "react";
import manifest from "./audioManifest.json";
import { drawShowcaseFrame } from "./showcase/drawSpectrum";

const PLAY_EVENT = "hba:showcase-play";

// Share a single AudioContext across all ShowcasePlayer decks so auditioning multiple
// products never hits browser per-tab AudioContext limits.
let sharedAudioCtx = null;
let sharedCtxUsers = 0;
const playingDecks = new Set();

function getSharedAudioContext() {
  if (typeof window === "undefined") return null;
  if (!sharedAudioCtx || sharedAudioCtx.state === "closed") {
    const AudioCtx = window.AudioContext || window.webkitAudioContext;
    if (!AudioCtx) return null;
    sharedAudioCtx = new AudioCtx();
  }
  return sharedAudioCtx;
}

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
  const [currentTime, setCurrentTime] = useState(0);
  const [duration, setDuration] = useState(tracks[0]?.duration ?? 0);
  const [autoAdvance, setAutoAdvance] = useState(true);
  const [hydrated, setHydrated] = useState(false);
  const [reducedMotion, setReducedMotion] = useState(false);

  const rootRef = useRef(null);
  const audioRef = useRef(null);
  const canvasRef = useRef(null);
  const stageRef = useRef(null);

  // Lazy Web Audio graph refs (connected only on first real playback)
  const hasAudioGraphRef = useRef(false);
  const analyserRef = useRef(null);
  const freqDataRef = useRef(null);
  const sourceNodeRef = useRef(null);

  // Canvas animation, seek & visibility refs
  const rafRef = useRef(0);
  const visibleRef = useRef(true);
  const playingRef = useRef(false);
  const energyRef = useRef(0);
  const hoverRef = useRef(null);
  const activeIndexRef = useRef(0);
  const activeTrackRef = useRef(tracks[0]);
  const currentTimeRef = useRef(0);
  const durationRef = useRef(tracks[0]?.duration ?? 0);
  const pendingSeekRef = useRef(null);
  const reducedMotionRef = useRef(false);
  const selectTrackRef = useRef(null);

  const headingId = useId();
  const activeTrack = tracks[activeIndex] ?? tracks[0];

  useEffect(() => {
    activeIndexRef.current = activeIndex;
    activeTrackRef.current = activeTrack;
    durationRef.current = duration || activeTrack?.duration || 1;
  }, [activeIndex, activeTrack, duration]);

  useEffect(() => {
    currentTimeRef.current = currentTime;
  }, [currentTime]);

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
      if (sharedAudioCtx?.state === "suspended") {
        sharedAudioCtx.resume().catch(() => {});
      }
      return;
    }
    const audio = audioRef.current;
    if (!audio) return;
    try {
      const ctx = getSharedAudioContext();
      if (!ctx) return;
      const source = ctx.createMediaElementSource(audio);
      const analyser = ctx.createAnalyser();
      analyser.fftSize = 256;
      analyser.smoothingTimeConstant = 0.78;
      source.connect(analyser);
      analyser.connect(ctx.destination);
      sourceNodeRef.current = source;
      analyserRef.current = analyser;
      freqDataRef.current = new Uint8Array(analyser.frequencyBinCount);
      if (!hasAudioGraphRef.current) {
        hasAudioGraphRef.current = true;
        sharedCtxUsers += 1;
      }
      if (ctx.state === "suspended") {
        ctx.resume().catch(() => {});
      }
    } catch {
      // Fallback gracefully to waveform progress without FFT if Web Audio is restricted
    }
  }, []);

  const drawFrame = useCallback(() => {
    drawShowcaseFrame({
      canvas: canvasRef.current,
      root: rootRef.current,
      audio: audioRef.current,
      track: activeTrackRef.current,
      hue,
      isPlaying: playingRef.current,
      reducedMotion: reducedMotionRef.current,
      analyser: analyserRef.current,
      freqData: freqDataRef.current,
      energyRef,
      hover: hoverRef.current,
      currentTime: currentTimeRef.current,
      duration: durationRef.current,
    });
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

  // Redraw static frame on track switch, seek, play/pause, or reduced-motion toggle
  // (skip redundant redraw when the 60 fps RAF loop is already active).
  useEffect(() => {
    if (!rafRef.current) drawFrame();
  }, [activeIndex, currentTime, duration, isPlaying, reducedMotion, drawFrame]);

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
    try {
      await audio.play();
    } catch {
      // Autoplay or decode failure handled by <audio> error/pause state
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
      const nextTrack = tracks[bounded];
      if (bounded === activeIndexRef.current) {
        if (shouldPlay) togglePlay();
        return;
      }
      activeIndexRef.current = bounded;
      activeTrackRef.current = nextTrack;
      currentTimeRef.current = 0;
      durationRef.current = nextTrack?.duration ?? 1;
      pendingSeekRef.current = null;
      setActiveIndex(bounded);
      setCurrentTime(0);
      setDuration(nextTrack?.duration ?? 0);
      if (audio && nextTrack) {
        audio.pause();
        audio.setAttribute("src", nextTrack.src);
        audio.load();
        if (shouldPlay) {
          // Call play synchronously inside user gesture so iOS Safari preserves activation.
          startPlayback();
        }
      }
    },
    [startPlayback, togglePlay, tracks],
  );
  selectTrackRef.current = selectTrack;

  // Release Web Audio graph & MediaSession handlers on unmount
  useEffect(() => {
    return () => {
      if (audioRef.current) audioRef.current.pause();
      playingDecks.delete(slug);
      sourceNodeRef.current?.disconnect();
      analyserRef.current?.disconnect();
      sourceNodeRef.current = null;
      analyserRef.current = null;
      if (hasAudioGraphRef.current) {
        hasAudioGraphRef.current = false;
        sharedCtxUsers = Math.max(0, sharedCtxUsers - 1);
        if (sharedCtxUsers === 0 && sharedAudioCtx) {
          sharedAudioCtx.close().catch(() => {});
          sharedAudioCtx = null;
        }
      } else if (playingDecks.size === 0 && sharedAudioCtx?.state === "running") {
        sharedAudioCtx.suspend().catch(() => {});
      }
      if (typeof navigator !== "undefined" && "mediaSession" in navigator) {
        try {
          if (navigator.mediaSession.metadata?.album === productName) {
            navigator.mediaSession.metadata = null;
            for (const action of ["play", "pause", "previoustrack", "nexttrack"]) {
              navigator.mediaSession.setActionHandler(action, null);
            }
          }
        } catch {
          // Ignore unsupported MediaSession cleanup
        }
      }
    };
  }, [productName, slug]);

  // Wire Media Session metadata & hardware media keys from live refs (never stale across synchronous track switches)
  const syncMediaSession = useCallback(() => {
    const track = activeTrackRef.current;
    if (typeof navigator === "undefined" || !("mediaSession" in navigator) || !track) return;
    try {
      navigator.mediaSession.metadata = new MediaMetadata({
        title: track.title,
        artist: "Human Being Audio",
        album: productName,
      });
      navigator.mediaSession.setActionHandler("play", () => startPlayback());
      navigator.mediaSession.setActionHandler("pause", () => audioRef.current?.pause());
      navigator.mediaSession.setActionHandler("previoustrack", () =>
        selectTrackRef.current?.(activeIndexRef.current - 1, true),
      );
      navigator.mediaSession.setActionHandler("nexttrack", () =>
        selectTrackRef.current?.(activeIndexRef.current + 1, true),
      );
    } catch {
      // Ignore if MediaSession actions are unsupported
    }
  }, [productName, startPlayback]);

  // Wire native <audio> events
  useEffect(() => {
    const audio = audioRef.current;
    if (!audio) return;

    const onPlay = () => {
      playingRef.current = true;
      playingDecks.add(slug);
      setIsPlaying(true);
      window.dispatchEvent(new CustomEvent(PLAY_EVENT, { detail: { slug } }));
      if (sharedAudioCtx?.state === "suspended") {
        sharedAudioCtx.resume().catch(() => {});
      }
      syncMediaSession();
      if (!reducedMotionRef.current) startLoop();
    };
    const onPause = () => {
      playingRef.current = false;
      playingDecks.delete(slug);
      setIsPlaying(false);
      energyRef.current = 0;
      if (rootRef.current) rootRef.current.style.setProperty("--energy", "0");
      if (playingDecks.size === 0 && sharedAudioCtx?.state === "running") {
        sharedAudioCtx.suspend().catch(() => {});
      }
      stopLoop();
      drawFrame();
    };
    const onTimeUpdate = () => {
      currentTimeRef.current = audio.currentTime;
      setCurrentTime(audio.currentTime);
    };
    const onLoadedMetadata = () => {
      if (Number.isFinite(audio.duration) && audio.duration > 0) {
        durationRef.current = audio.duration;
        setDuration(audio.duration);
      }
      if (pendingSeekRef.current != null) {
        const target =
          Number.isFinite(audio.duration) && audio.duration > 0
            ? Math.min(pendingSeekRef.current, audio.duration)
            : pendingSeekRef.current;
        pendingSeekRef.current = null;
        audio.currentTime = target;
        currentTimeRef.current = target;
        setCurrentTime(target);
      }
    };
    const onError = () => {
      playingRef.current = false;
      playingDecks.delete(slug);
      setIsPlaying(false);
      stopLoop();
    };
    const onEnded = () => {
      if (autoAdvance && tracks.length > 1) {
        selectTrackRef.current?.(activeIndexRef.current + 1, true);
      } else {
        setIsPlaying(false);
        setCurrentTime(0);
      }
    };

    audio.addEventListener("play", onPlay);
    audio.addEventListener("pause", onPause);
    audio.addEventListener("timeupdate", onTimeUpdate);
    audio.addEventListener("loadedmetadata", onLoadedMetadata);
    audio.addEventListener("error", onError);
    audio.addEventListener("ended", onEnded);
    return () => {
      audio.removeEventListener("play", onPlay);
      audio.removeEventListener("pause", onPause);
      audio.removeEventListener("timeupdate", onTimeUpdate);
      audio.removeEventListener("loadedmetadata", onLoadedMetadata);
      audio.removeEventListener("error", onError);
      audio.removeEventListener("ended", onEnded);
    };
  }, [autoAdvance, drawFrame, slug, startLoop, stopLoop, syncMediaSession, tracks.length]);

  const handleScrub = (event) => {
    const next = parseFloat(event.target.value);
    currentTimeRef.current = next;
    setCurrentTime(next);
    const audio = audioRef.current;
    if (audio) {
      if (audio.readyState > 0) {
        pendingSeekRef.current = null;
        audio.currentTime = next;
      } else {
        pendingSeekRef.current = next;
      }
    }
    if (!rafRef.current) drawFrame();
  };

  const handlePointerMove = (event) => {
    const rect = event.currentTarget.getBoundingClientRect();
    const usable = rect.width - 20;
    if (usable <= 0) return;
    hoverRef.current = Math.max(0, Math.min(1, (event.clientX - rect.left - 10) / usable));
    if (!rafRef.current) drawFrame();
  };

  const handlePointerLeave = () => {
    hoverRef.current = null;
    if (!rafRef.current) drawFrame();
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

      {/* Native <audio controls> stays visible without JS; hidden visually once custom deck hydrates.
          Keep initial src fixed in JSX so React does not re-set the src attribute on track switch
          and abort the synchronous user-gesture play() initiated in selectTrack(). */}
      <div className="showcase__native">
        <audio
          ref={audioRef}
          controls
          preload="none"
          aria-label={`${productName} audio example`}
          src={tracks[0]?.src}
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
              aria-label="Auto-advance clips"
              aria-pressed={autoAdvance ? "true" : "false"}
              onClick={() => setAutoAdvance((v) => !v)}
              title={autoAdvance ? "Auto-advance to next clip" : "Stop after current clip"}
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
          onPointerLeave={handlePointerLeave}
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
