// Interactive Hero dock controls: vowel F1/F2 pad, macros, form picker, freeze/hit, and plugin card tracking.

const VOWELS = [
  { v: "i", f1: 280, f2: 2250 },
  { v: "e", f1: 450, f2: 1950 },
  { v: "a", f1: 750, f2: 1300 },
  { v: "o", f1: 450, f2: 900 },
  { v: "u", f1: 310, f2: 800 },
];
const F2_FRONT = 2400;
const F2_BACK = 700;
const F1_CLOSE = 250;
const F1_OPEN = 800;
const padX = (x) => 8 + x * 84;
const padY = (y) => 9 + y * 58;
const LOG_LO = Math.log(80);
const LOG_HI = Math.log(5000);
const fx = (f) => 2 + 96 * (Math.log(f) - LOG_LO) / (LOG_HI - LOG_LO);
const heightWord = (y) => (y < 0.33 ? "close" : y < 0.66 ? "mid" : "open");
const backWord = (x) => (x < 0.33 ? "front" : x < 0.66 ? "central" : "back");
const DIVS = [8, 16, 32, 64];
const DIV_NAMES = ["8ths", "16ths", "32nds", "64ths"];
const noiseRand = (i) => {
  const v = Math.sin(i * 12.9898) * 43758.5453;
  return v - Math.floor(v);
};

function envelope(f1, f2, f3) {
  const peaks = [
    [f1, 1, 0.12],
    [f2, 0.78, 0.1],
    [f3, 0.5, 0.08],
  ];
  const amp = (f) =>
    Math.min(
      1,
      0.05 +
        peaks.reduce(
          (a, [F, A, s]) => a + A * Math.exp(-((Math.log(f) - Math.log(F)) ** 2) / (2 * s * s)),
          0,
        ),
    );
  const pts = Array.from({ length: 81 }, (_, i) => {
    const f = Math.exp(LOG_LO + ((LOG_HI - LOG_LO) * i) / 80);
    return `${fx(f).toFixed(2)} ${(99 - amp(f) * 25).toFixed(2)}`;
  });
  let comb = "";
  for (let h = 110; h < 5000; h += 110) comb += `M${fx(h).toFixed(2)} 99V${(99 - amp(h) * 22).toFixed(2)}`;
  return { line: `M${pts.join("L")}`, fill: `M2 99L${pts.join("L")}L98 99Z`, comb };
}

export function mountDockControls({ root, dock, pad, formPicker, cards, byId, stage, reducedMotion, signal }) {
  const $ = (selector, scope = document) => scope.querySelector(selector);
  const $$ = (selector, scope = document) => [...scope.querySelectorAll(selector)];
  const seedOf = (p) =>
    [...p.id].reduce((h, c) => Math.imul(h ^ c.charCodeAt(0), 16777619) >>> 0, 2166136261) / 2 ** 32;
  const cleanups = [];

  $("#stage")?.style.setProperty("--stage-hue", stage.hue);
  dock.style.setProperty("--hue", stage.hue);

  function syncWindowLabel() {
    const p = byId[stage.pluginId];
    $("[data-window-name]").textContent = p.name;
    $("[data-window-kind]").textContent = `${stage.form} · ${p.statusLabel.toLowerCase()}`;
  }

  formPicker.addEventListener(
    "change",
    (e) => {
      stage.form = e.target.value;
      syncWindowLabel();
      stage.wake();
    },
    { signal },
  );

  // Vowel space setup
  const vowels = VOWELS.map((v) => ({ ...v }));
  for (const existing of $$(".vowel__v", pad)) existing.remove();
  for (const V of vowels) {
    V.x = (F2_FRONT - V.f2) / (F2_FRONT - F2_BACK);
    V.y = (V.f1 - F1_CLOSE) / (F1_OPEN - F1_CLOSE);
    V.el = Object.assign(document.createElement("span"), { className: "vowel__v", textContent: V.v });
    V.el.style.cssText = `--vx:${V.x.toFixed(3)};--vy:${V.y.toFixed(3)}`;
    pad.querySelector(".vowel__puck").before(V.el);
  }
  cleanups.push(() => {
    for (const V of vowels) V.el?.remove();
  });
  $("[data-arc]").setAttribute(
    "points",
    vowels.map((V) => `${padX(V.x).toFixed(1)},${padY(V.y).toFixed(1)}`).join(" "),
  );

  function updateVowel() {
    const { formant: x, open: y } = stage.macros;
    pad.style.setProperty("--x", x);
    pad.style.setProperty("--y", y);
    root.style.setProperty("--formant", x.toFixed(3));
    let best = vowels[0];
    let bestW = 0;
    for (const V of vowels) {
      const dx = (x - V.x) * 1.6;
      const dy = y - V.y;
      const w = Math.exp(-(dx * dx + dy * dy) / 0.07);
      V.el.style.setProperty("--w", w.toFixed(3));
      if (w > bestW) {
        bestW = w;
        best = V;
      }
    }
    const f2 = F2_FRONT - x * (F2_FRONT - F2_BACK);
    const f1 = F1_CLOSE + y * (F1_OPEN - F1_CLOSE);
    const f3 = 2500 + (f2 - 1500) * 0.3;
    const env = envelope(f1, f2, f3);
    $("[data-env-line]").setAttribute("d", env.line);
    $("[data-env-fill]").setAttribute("d", env.fill);
    $("[data-comb]").setAttribute("d", env.comb);
    $('[data-readout="vowel"]').value = `${bestW > 0.55 ? "" : "≈ "}/${best.v}/`;
    $('[data-readout="hz"]').textContent = `F1 ${Math.round(f1)} · F2 ${Math.round(f2)} Hz`;
    $("#m-formant").setAttribute(
      "aria-valuetext",
      `${backWord(x)}, F2 ${Math.round(f2)} hertz, nearest vowel ${best.v}`,
    );
    $("#m-open").setAttribute(
      "aria-valuetext",
      `${heightWord(y)}, F1 ${Math.round(f1)} hertz, nearest vowel ${best.v}`,
    );
  }

  function setMacro(name, v) {
    stage.macros[name] = v;
    const input = $(`[data-macro="${name}"]`);
    if (name === "formant" || name === "open") updateVowel();
    if (name === "slice") {
      const i = Math.min(3, Math.floor(v * 4));
      dock.style.setProperty("--div", DIVS[i]);
      $('[data-readout="slice"]').value = DIV_NAMES[i];
      input.setAttribute("aria-valuetext", DIV_NAMES[i]);
    }
    if (name === "chaos") {
      dock.style.setProperty("--chaos", v.toFixed(3));
      let d = "M0 8";
      for (let i = 1; i <= 60; i++)
        d += `L${i * 2} ${(8 + Math.sin(i * 0.55) * 3.2 * (1 - v) + (noiseRand(i) - 0.5) * 14 * v).toFixed(2)}`;
      $("[data-noise]").setAttribute("d", d);
      $('[data-readout="chaos"]').value = `${Math.round(v * 100)}%`;
      input.setAttribute("aria-valuetext", `${Math.round(v * 100)} percent`);
    }
    stage.wake();
  }

  $$("[data-macro]").forEach((input) => {
    input.addEventListener("input", () => setMacro(input.dataset.macro, input.valueAsNumber / 100), { signal });
    setMacro(input.dataset.macro, input.valueAsNumber / 100);
  });

  pad.addEventListener(
    "pointerdown",
    (e) => {
      pad.setPointerCapture(e.pointerId);
      const move = (ev) => {
        const r = pad.getBoundingClientRect();
        const x = Math.min(1, Math.max(0, (((ev.clientX - r.left) / r.width) * 100 - 8) / 84));
        const y = Math.min(1, Math.max(0, (((ev.clientY - r.top) / r.height) * 100 - 9) / 58));
        $("#m-formant").value = Math.round(x * 100);
        $("#m-open").value = Math.round(y * 100);
        stage.macros.formant = x;
        setMacro("open", y);
      };
      move(e);
      const stop = () => {
        pad.removeEventListener("pointermove", move);
        pad.removeEventListener("pointerup", stop);
        pad.removeEventListener("pointercancel", stop);
      };
      pad.addEventListener("pointermove", move, { signal });
      pad.addEventListener("pointerup", stop, { signal });
      pad.addEventListener("pointercancel", stop, { signal });
    },
    { signal },
  );

  function showPlugin(id) {
    const p = byId[id];
    if (!p || stage.pluginId === id) return;
    Object.assign(stage, { form: p.form, pluginId: id, hue: p.hue, bpm: p.bpm, seed: seedOf(p) });
    $("#stage").style.setProperty("--stage-hue", p.hue);
    dock.style.setProperty("--hue", p.hue);
    const radio = $(`input[name="form"][value="${stage.form}"]`);
    if (radio) radio.checked = true;
    $$(".plugin").forEach((c) => c.toggleAttribute("data-active", c.dataset.plugin === id));
    syncWindowLabel();
    stage.wake();
  }
  syncWindowLabel();

  let centreId = null;
  let hoverId = null;
  for (const card of cards) {
    card.addEventListener("pointerenter", () => { hoverId = card.dataset.plugin; showPlugin(hoverId); }, { signal });
    card.addEventListener("pointerleave", () => { hoverId = null; if (centreId) showPlugin(centreId); }, { signal });
    card.addEventListener("focusin", () => showPlugin(card.dataset.plugin), { signal });
  }
  const centreIO = new IntersectionObserver(
    (entries) => {
      for (const e of entries) {
        if (e.isIntersecting) {
          centreId = e.target.dataset.plugin;
          if (!hoverId) showPlugin(centreId);
        }
      }
    },
    { rootMargin: "-48% 0px -48% 0px" },
  );
  cards.forEach((c) => centreIO.observe(c));
  cleanups.push(() => centreIO.disconnect());

  $("[data-hit]").addEventListener("click", () => { stage.hit = 1; stage.wake(); }, { signal });
  const freezeBtn = $("[data-freeze]");
  const syncFreeze = () => freezeBtn.setAttribute("aria-pressed", String(stage.frozen));
  freezeBtn.addEventListener("click", () => { stage.frozen = !stage.frozen; syncFreeze(); stage.wake(); }, { signal });
  syncFreeze();

  reducedMotion.addEventListener(
    "change",
    () => {
      stage.frozen = reducedMotion.matches;
      syncFreeze();
      stage.wake();
    },
    { signal },
  );

  return () => {
    for (const fn of cleanups) fn();
  };
}
