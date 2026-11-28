// Client-side artwork orchestrator. The catalogue and all downloads are server-rendered.
import { mountDockControls } from "./sculpture/dockControls";
import { startHoberman } from "./sculpture/hoberman";
import { hasHardwareGraphics, startStage } from "./sculpture/stage3d";

export function mountSculpture() {
  const $ = (selector, root = document) => root.querySelector(selector);
  const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];
  const ac = new AbortController();
  const { signal } = ac;
  let disposed = false;
  let stageCleanup = null;
  const cleanups = [];

  const reducedMotion = matchMedia("(prefers-reduced-motion: reduce)");
  const cards = $$(".plugin[data-plugin]");
  const root = document.documentElement;
  const dock = $(".hero__dock");
  const pad = $("[data-vowel-pad]");
  const formPicker = $("#form-picker");
  if (!cards.length || !dock || !pad || !formPicker) return () => {};

  const PLUGINS = cards.map((card) => ({
    id: card.dataset.plugin,
    name: card.querySelector("h3").textContent,
    form: card.dataset.form,
    hue: Number(card.dataset.hue),
    bpm: Number(card.dataset.bpm),
    statusLabel: card.dataset.kind,
  }));
  const byId = Object.fromEntries(PLUGINS.map((p) => [p.id, p]));
  const seedOf = (p) =>
    [...p.id].reduce((h, c) => Math.imul(h ^ c.charCodeAt(0), 16777619) >>> 0, 2166136261) / 2 ** 32;

  const stage = {
    form: PLUGINS[0].form,
    pluginId: PLUGINS[0].id,
    hue: PLUGINS[0].hue,
    bpm: PLUGINS[0].bpm,
    seed: seedOf(PLUGINS[0]),
    macros: { slice: 0.5, formant: 0.6, open: 0.8, chaos: 0.15 },
    frozen: reducedMotion.matches,
    hit: 0,
    wake: () => {},
  };

  cleanups.push(mountDockControls({ root, dock, pad, formPicker, cards, byId, stage, seedOf, reducedMotion, signal }));

  const backendEl = $("[data-backend]");
  const enableScene = $("[data-enable-scene]");
  async function enableStage() {
    if (disposed) return;
    enableScene.hidden = true;
    try {
      stageCleanup = await startStage({
        root,
        stage,
        reducedMotion,
        backendEl,
        signal,
        isDisposed: () => disposed,
      });
    } catch (err) {
      if (disposed) return;
      root.dataset.stage = "fallback";
      backendEl.textContent = "3D unavailable";
      console.warn("[stage]", err);
    }
  }

  enableScene.addEventListener("click", enableStage, { once: true, signal });
  hasHardwareGraphics()
    .catch(() => false)
    .then((hardware) => {
      if (disposed) return;
      if (hardware) return enableStage();
      root.dataset.stage = "fallback";
      backendEl.textContent = "3D paused · software graphics";
      enableScene.hidden = false;
    });

  cleanups.push(startHoberman(reducedMotion, signal));

  return function cleanupSculpture() {
    disposed = true;
    ac.abort();
    stageCleanup?.();
    stageCleanup = null;
    for (const fn of cleanups) fn();
    delete root.dataset.stage;
  };
}
