"use client";

import { useCallback, useEffect, useRef, useState } from "react";
import { POLE_FIELD_WORKLET_CODE } from "../dspMath";

const LAB_STATUS_LABELS = {
  lab1: "Playing Lab 1 Dipole",
  lab3: "Playing 14-Pole Lattice",
  lab4: "Playing ADAA2 Tone",
  lab5: "Playing Burg LPC Imprint",
};

/**
 * Manages the shared Web Audio / AudioWorklet instance across all interactive
 * Pole Field Field Guide labs, serialized against double-init races.
 */
export function usePoleFieldAudio() {
  const [activeAudioLab, setActiveAudioLab] = useState(null);
  const [audioStatus, setAudioStatus] = useState("Idle");
  const audioCtxRef = useRef(null);
  const workletRef = useRef(null);
  const audioInitPromiseRef = useRef(null);
  const unmountedRef = useRef(false);

  const ensureAudioEngine = useCallback(async () => {
    if (unmountedRef.current) return null;
    if (workletRef.current && audioCtxRef.current) {
      if (audioCtxRef.current.state === "suspended") {
        await audioCtxRef.current.resume();
      }
      return workletRef.current;
    }
    if (audioInitPromiseRef.current) {
      return audioInitPromiseRef.current;
    }
    const init = (async () => {
      const Ctx = window.AudioContext || window.webkitAudioContext;
      if (!Ctx) {
        setAudioStatus("Web Audio unavailable");
        return null;
      }
      let ctx;
      try {
        ctx = new Ctx({ sampleRate: 48000 });
      } catch {
        ctx = new Ctx();
      }
      if (!ctx.audioWorklet) {
        ctx.close().catch(() => {});
        setAudioStatus("AudioWorklet unsupported");
        return null;
      }
      const blob = new Blob([POLE_FIELD_WORKLET_CODE], { type: "application/javascript" });
      const url = URL.createObjectURL(blob);
      try {
        await ctx.audioWorklet.addModule(url);
      } catch (err) {
        ctx.close().catch(() => {});
        throw err;
      } finally {
        URL.revokeObjectURL(url);
      }
      if (unmountedRef.current) {
        ctx.close().catch(() => {});
        return null;
      }
      const node = new AudioWorkletNode(ctx, "pole-field-bench", {
        numberOfInputs: 0,
        numberOfOutputs: 1,
        outputChannelCount: [2],
      });
      node.connect(ctx.destination);
      audioCtxRef.current = ctx;
      workletRef.current = node;
      return node;
    })();
    audioInitPromiseRef.current = init;
    try {
      return await init;
    } finally {
      audioInitPromiseRef.current = null;
    }
  }, []);

  const stopAllAudio = useCallback(() => {
    setActiveAudioLab(null);
    setAudioStatus("Idle");
    if (workletRef.current) {
      workletRef.current.port.postMessage({ type: "state", playing: false });
    }
    if (audioCtxRef.current?.state === "running") {
      audioCtxRef.current.suspend().catch(() => {});
    }
  }, []);

  const toggleLabAudio = useCallback(
    async (labId) => {
      if (activeAudioLab === labId) {
        stopAllAudio();
        return;
      }
      try {
        const node = await ensureAudioEngine();
        if (!node) return;
        setActiveAudioLab(labId);
        setAudioStatus(LAB_STATUS_LABELS[labId] || "Playing");
      } catch {
        setAudioStatus("Audio blocked");
      }
    },
    [activeAudioLab, ensureAudioEngine, stopAllAudio],
  );

  const postAudioState = useCallback(
    (labId, state) => {
      const node = workletRef.current;
      if (!node || activeAudioLab !== labId) return;
      node.port.postMessage({ type: "state", playing: true, ...state });
    },
    [activeAudioLab],
  );

  useEffect(() => {
    unmountedRef.current = false;
    return () => {
      unmountedRef.current = true;
      if (audioCtxRef.current) {
        audioCtxRef.current.close().catch(() => {});
        audioCtxRef.current = null;
        workletRef.current = null;
      }
    };
  }, []);

  return {
    activeAudioLab,
    audioStatus,
    toggleLabAudio,
    stopAllAudio,
    postAudioState,
  };
}
