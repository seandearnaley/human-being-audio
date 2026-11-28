"use client";

import Image from "next/image";
import { useEffect, useRef, useState } from "react";
import { flushSync } from "react-dom";

// Shared with ShowcasePlayer: whoever starts playing announces it and everyone else pauses.
const PLAY_EVENT = "hba:showcase-play";
const CARD_SIZES = "(max-width: 800px) 93vw, (max-width: 1152px) 92vw, 760px";

const plainClick = (event) =>
  event.button === 0 && !event.metaKey && !event.ctrlKey && !event.shiftKey && !event.altKey;

function clock(seconds) {
  const whole = Math.round(seconds);
  return `${Math.floor(whole / 60)}:${String(whole % 60).padStart(2, "0")}`;
}

// The video owns the system media controls while it plays (keyboard media keys, lock screen),
// so they stop reaching an audio deck that ShowcasePlayer registered earlier.
function claimMediaSession(video, title) {
  if (!("mediaSession" in navigator)) return;
  try {
    navigator.mediaSession.metadata = new MediaMetadata({ title, artist: "Human Being Audio", album: "Video" });
    navigator.mediaSession.setActionHandler("play", () => video.play().catch(() => {}));
    navigator.mediaSession.setActionHandler("pause", () => video.pause());
    navigator.mediaSession.setActionHandler("previoustrack", null);
    navigator.mediaSession.setActionHandler("nexttrack", null);
  } catch {
    // MediaSession actions are optional
  }
}

// The poster is a link to the MP4, so the demo still plays without JavaScript. With it, the
// click swaps in a <video>; nothing of the video downloads until then, and it starts with sound
// only because the visitor asked for it.
export function ProductVideo({ slug, name, video, posterAlt }) {
  const [started, setStarted] = useState(false);
  const videoRef = useRef(null);
  const source = `${slug}:video`;
  const mp4 = video.sources.find((s) => s.src.endsWith(".mp4")) ?? video.sources[0];

  useEffect(() => {
    const onOtherPlay = (event) => {
      if (event.detail?.slug !== source) videoRef.current?.pause();
    };
    window.addEventListener(PLAY_EVENT, onOtherPlay);
    return () => window.removeEventListener(PLAY_EVENT, onOtherPlay);
  }, [source]);

  return (
    <figure className="product-video" data-product-video={slug}>
      <div className="product-video__frame">
        {started ? (
          <video
            ref={videoRef}
            className="product-video__media"
            width={video.width}
            height={video.height}
            poster={video.poster}
            controls
            autoPlay
            playsInline
            preload="auto"
            aria-label={video.title}
            onPlay={(event) => {
              window.dispatchEvent(new CustomEvent(PLAY_EVENT, { detail: { slug: source } }));
              claimMediaSession(event.currentTarget, video.title);
            }}
          >
            {video.sources.map((s) => (
              <source key={s.src} src={s.src} type={s.type} />
            ))}
          </video>
        ) : (
          <a
            className="product-video__poster"
            href={mp4.src}
            aria-label={`Play the ${clock(video.seconds)} ${name} demo video, with sound`}
            onClick={(event) => {
              if (!plainClick(event)) return;
              event.preventDefault();
              // Mount and start the video inside the click, so Safari counts it as user-initiated.
              flushSync(() => setStarted(true));
              videoRef.current?.focus({ preventScroll: true });
              videoRef.current?.play().catch(() => {});
            }}
          >
            <Image
              src={video.poster}
              width={video.width}
              height={video.height}
              alt={posterAlt}
              sizes={CARD_SIZES}
              className="studio-interface product-video__media"
            />
            <span className="product-video__play" aria-hidden="true">
              <svg viewBox="0 0 24 24" width="28" height="28">
                <path d="M8 5.5v13l10.5-6.5z" fill="currentColor" />
              </svg>
            </span>
            <span className="product-video__badge mono" aria-hidden="true">
              {video.badge} · {clock(video.seconds)} · sound on
            </span>
          </a>
        )}
      </div>
      <figcaption className="product-video__caption">{video.caption}</figcaption>
    </figure>
  );
}

// Thumbnails link to the full-size files (useful without JavaScript); with it they open a
// native modal <dialog> that steps through the set with the arrow keys and closes on Escape.
export function ProductGallery({ name, items }) {
  const dialogRef = useRef(null);
  const figureRef = useRef(null);
  const pressedBackdrop = useRef(false);
  const [index, setIndex] = useState(0);
  const [open, setOpen] = useState(false);
  const item = items[index];

  const step = (delta) => setIndex((i) => (i + delta + items.length) % items.length);

  // Start on the picture, so a screen reader announces the shot rather than the first button.
  useEffect(() => {
    if (open) figureRef.current?.focus({ preventScroll: true });
  }, [open]);

  return (
    <div className="product-gallery">
      <ul className="product-gallery__grid" role="list" aria-label={`${name} screenshots`}>
        {items.map((shot, i) => (
          <li key={shot.src}>
            <a
              className="product-gallery__thumb"
              href={shot.src}
              onClick={(event) => {
                if (!plainClick(event)) return;
                event.preventDefault();
                setIndex(i);
                setOpen(true);
                dialogRef.current?.showModal();
              }}
            >
              <Image
                src={shot.src}
                width={shot.width}
                height={shot.height}
                alt={shot.alt}
                sizes="(max-width: 40rem) 46vw, (max-width: 1152px) 24vw, 186px"
              />
              <span className="product-gallery__label mono">{shot.label}</span>
            </a>
          </li>
        ))}
      </ul>
      {/* The dialog has no padding, so only a press that starts and ends on the backdrop closes it. */}
      <dialog
        ref={dialogRef}
        className="product-lightbox"
        aria-label={`${name} screenshots`}
        onClose={() => setOpen(false)}
        onPointerDown={(event) => {
          pressedBackdrop.current = event.target === dialogRef.current;
        }}
        onClick={(event) => {
          if (pressedBackdrop.current && event.target === dialogRef.current) dialogRef.current.close();
          pressedBackdrop.current = false;
        }}
        onKeyDown={(event) => {
          if (event.key === "ArrowRight") step(1);
          else if (event.key === "ArrowLeft") step(-1);
        }}
      >
        <div className="product-lightbox__panel">
          {open && (
            <figure ref={figureRef} className="product-lightbox__figure" tabIndex={-1}>
              <Image
                key={item.src}
                src={item.src}
                width={item.width}
                height={item.height}
                alt={item.alt}
                sizes="(max-width: 1700px) 94vw, 1600px"
                className="product-lightbox__image"
              />
              <figcaption className="product-lightbox__caption" aria-live="polite">
                <span className="mono">
                  {index + 1} / {items.length} · {item.label}
                </span>
                <span>{item.alt}</span>
              </figcaption>
            </figure>
          )}
          <div className="product-lightbox__controls">
            <button type="button" className="btn" onClick={() => step(-1)} aria-label="Previous screenshot">
              ←
            </button>
            <button type="button" className="btn" onClick={() => step(1)} aria-label="Next screenshot">
              →
            </button>
            <button type="button" className="btn" onClick={() => dialogRef.current?.close()}>
              Close
            </button>
          </div>
        </div>
      </dialog>
    </div>
  );
}
