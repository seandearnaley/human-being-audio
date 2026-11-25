// Client-side artwork. The catalogue and all downloads are server-rendered.
export function mountSculpture() {
const $ = (selector, root = document) => root.querySelector(selector);
const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];
const ac = new AbortController();
const { signal } = ac;
let disposed = false;
let stageCleanup = null;
const cleanups = [];

const reducedMotion = matchMedia('(prefers-reduced-motion: reduce)');
const cards = $$('.plugin[data-plugin]');
const root = document.documentElement;
const dock = $('.hero__dock');
const pad = $('[data-vowel-pad]');
const formPicker = $('#form-picker');
if (!cards.length || !dock || !pad || !formPicker) return () => {};

const PLUGINS = cards.map(card => ({
  id: card.dataset.plugin, name: card.querySelector('h3').textContent, form: card.dataset.form,
  hue: Number(card.dataset.hue), bpm: Number(card.dataset.bpm), statusLabel: card.dataset.kind
}));
const byId = Object.fromEntries(PLUGINS.map(p => [p.id,p]));
const formOf = p => p.form;
const seedOf = p => [...p.id].reduce((h,c) => Math.imul(h ^ c.charCodeAt(0),16777619) >>> 0,2166136261) / 2 ** 32;
const stage = {
  form: formOf(PLUGINS[0]),
  pluginId: PLUGINS[0].id,
  hue: PLUGINS[0].hue,
  bpm: PLUGINS[0].bpm,
  seed: seedOf(PLUGINS[0]),
  macros: { slice: 0.5, formant: 0.6, open: 0.8, chaos: 0.15 },
  frozen: reducedMotion.matches,
  hit: 0,
  wake: () => {},        // replaced by the renderer once it's running
};

$('#stage')?.style.setProperty('--stage-hue', stage.hue);
dock.style.setProperty('--hue', stage.hue);

formPicker.addEventListener('change', (e) => { stage.form = e.target.value; syncWindowLabel(); stage.wake(); }, { signal });

/* ---- Vowel space: the five-vowel F1/F2 chart. F1 (height) runs down, F2 (front→back) runs across. ---- */
const VOWELS = [
  { v: 'i', f1: 280, f2: 2250 }, { v: 'e', f1: 450, f2: 1950 }, { v: 'a', f1: 750, f2: 1300 },
  { v: 'o', f1: 450, f2: 900 },  { v: 'u', f1: 310, f2: 800 },
];
const F2_FRONT = 2400, F2_BACK = 700, F1_CLOSE = 250, F1_OPEN = 800;
const padX = (x) => 8 + x * 84, padY = (y) => 9 + y * 58;                 // 0–1 → % of the pad (matches the CSS)
for (const existing of $$('.vowel__v', pad)) existing.remove();
for (const V of VOWELS) {
  V.x = (F2_FRONT - V.f2) / (F2_FRONT - F2_BACK);
  V.y = (V.f1 - F1_CLOSE) / (F1_OPEN - F1_CLOSE);
  V.el = Object.assign(document.createElement('span'), { className: 'vowel__v', textContent: V.v });
  V.el.style.cssText = `--vx:${V.x.toFixed(3)};--vy:${V.y.toFixed(3)}`;
  pad.querySelector('.vowel__puck').before(V.el);
}
cleanups.push(() => { for (const V of VOWELS) V.el?.remove(); });
$('[data-arc]').setAttribute('points', VOWELS.map((V) => `${padX(V.x).toFixed(1)},${padY(V.y).toFixed(1)}`).join(' '));

const LOG_LO = Math.log(80), LOG_HI = Math.log(5000);
const fx = (f) => 2 + 96 * (Math.log(f) - LOG_LO) / (LOG_HI - LOG_LO);
/** Spectral envelope with three formant peaks, as % coordinates along the bottom of the pad. */
function envelope(f1, f2, f3) {
  const peaks = [[f1, 1, 0.12], [f2, 0.78, 0.1], [f3, 0.5, 0.08]];
  const amp = (f) => Math.min(1, 0.05 + peaks.reduce((a, [F, A, s]) => a + A * Math.exp(-((Math.log(f) - Math.log(F)) ** 2) / (2 * s * s)), 0));
  const pts = Array.from({ length: 81 }, (_, i) => {
    const f = Math.exp(LOG_LO + (LOG_HI - LOG_LO) * i / 80);
    return `${fx(f).toFixed(2)} ${(99 - amp(f) * 25).toFixed(2)}`;
  });
  let comb = '';
  for (let h = 110; h < 5000; h += 110) comb += `M${fx(h).toFixed(2)} 99V${(99 - amp(h) * 22).toFixed(2)}`;   // harmonics of A2
  return { line: `M${pts.join('L')}`, fill: `M2 99L${pts.join('L')}L98 99Z`, comb };
}
const heightWord = (y) => (y < 0.33 ? 'close' : y < 0.66 ? 'mid' : 'open');
const backWord = (x) => (x < 0.33 ? 'front' : x < 0.66 ? 'central' : 'back');
function updateVowel() {
  const { formant: x, open: y } = stage.macros;
  pad.style.setProperty('--x', x); pad.style.setProperty('--y', y);
  root.style.setProperty('--formant', x.toFixed(3));                       // the headline's width axis follows the vowel
  let best = VOWELS[0], bestW = 0;
  for (const V of VOWELS) {
    const dx = (x - V.x) * 1.6, dy = y - V.y;                             // 1.6 ≈ the pad's aspect, so "near" is round
    const w = Math.exp(-(dx * dx + dy * dy) / 0.07);
    V.el.style.setProperty('--w', w.toFixed(3));
    if (w > bestW) { bestW = w; best = V; }
  }
  const f2 = F2_FRONT - x * (F2_FRONT - F2_BACK), f1 = F1_CLOSE + y * (F1_OPEN - F1_CLOSE), f3 = 2500 + (f2 - 1500) * 0.3;
  const env = envelope(f1, f2, f3);
  $('[data-env-line]').setAttribute('d', env.line);
  $('[data-env-fill]').setAttribute('d', env.fill);
  $('[data-comb]').setAttribute('d', env.comb);
  $('[data-readout="vowel"]').value = `${bestW > 0.55 ? '' : '≈ '}/${best.v}/`;
  $('[data-readout="hz"]').textContent = `F1 ${Math.round(f1)} · F2 ${Math.round(f2)} Hz`;
  $('#m-formant').setAttribute('aria-valuetext', `${backWord(x)}, F2 ${Math.round(f2)} hertz, nearest vowel ${best.v}`);
  $('#m-open').setAttribute('aria-valuetext', `${heightWord(y)}, F1 ${Math.round(f1)} hertz, nearest vowel ${best.v}`);
}

const DIVS = [8, 16, 32, 64], DIV_NAMES = ['8ths', '16ths', '32nds', '64ths'];
const noiseRand = (i) => { const v = Math.sin(i * 12.9898) * 43758.5453; return v - Math.floor(v); };
function setMacro(name, v) {
  stage.macros[name] = v;
  const input = $(`[data-macro="${name}"]`);
  if (name === 'formant' || name === 'open') updateVowel();
  if (name === 'slice') {
    const i = Math.min(3, Math.floor(v * 4));
    dock.style.setProperty('--div', DIVS[i]);
    $('[data-readout="slice"]').value = DIV_NAMES[i];
    input.setAttribute('aria-valuetext', DIV_NAMES[i]);
  }
  if (name === 'chaos') {
    dock.style.setProperty('--chaos', v.toFixed(3));
    let d = 'M0 8';
    for (let i = 1; i <= 60; i++) d += `L${i * 2} ${(8 + Math.sin(i * 0.55) * 3.2 * (1 - v) + (noiseRand(i) - 0.5) * 14 * v).toFixed(2)}`;
    $('[data-noise]').setAttribute('d', d);
    $('[data-readout="chaos"]').value = `${Math.round(v * 100)}%`;
    input.setAttribute('aria-valuetext', `${Math.round(v * 100)} percent`);
  }
  stage.wake();
}
$$('[data-macro]').forEach((input) => {
  input.addEventListener('input', () => setMacro(input.dataset.macro, input.valueAsNumber / 100), { signal });
  setMacro(input.dataset.macro, input.valueAsNumber / 100);
});

// The pad is a pointer shortcut for the two edge sliders (which carry keyboard + AT access).
pad.addEventListener('pointerdown', (e) => {
  pad.setPointerCapture(e.pointerId);
  const move = (ev) => {
    const r = pad.getBoundingClientRect();
    const x = Math.min(1, Math.max(0, ((ev.clientX - r.left) / r.width * 100 - 8) / 84));
    const y = Math.min(1, Math.max(0, ((ev.clientY - r.top) / r.height * 100 - 9) / 58));
    $('#m-formant').value = Math.round(x * 100);
    $('#m-open').value = Math.round(y * 100);
    stage.macros.formant = x;
    setMacro('open', y);
  };
  move(e);
  const stop = () => { pad.removeEventListener('pointermove', move); pad.removeEventListener('pointerup', stop); pad.removeEventListener('pointercancel', stop); };
  pad.addEventListener('pointermove', move, { signal });
  pad.addEventListener('pointerup', stop, { signal });
  pad.addEventListener('pointercancel', stop, { signal });
}, { signal });

function syncWindowLabel() {
  const p = byId[stage.pluginId];
  $('[data-window-name]').textContent = p.name;
  $('[data-window-kind]').textContent = `${stage.form} · ${p.statusLabel.toLowerCase()}`;
}
/** A plugin doesn't own a sculpture: it picks a shared form, tints it with its hue and varies it with its seed. */
function showPlugin(id) {
  const p = byId[id];
  if (!p || stage.pluginId === id) return;
  Object.assign(stage, { form: formOf(p), pluginId: id, hue: p.hue, bpm: p.bpm, seed: seedOf(p) });
  $('#stage').style.setProperty('--stage-hue', p.hue);
  dock.style.setProperty('--hue', p.hue);
  const radio = $(`input[name="form"][value="${stage.form}"]`);
  if (radio) radio.checked = true;
  $$('.plugin').forEach((c) => c.toggleAttribute('data-active', c.dataset.plugin === id));
  syncWindowLabel();
  stage.wake();
}
syncWindowLabel();

// Hover / focus a plugin → its tinted form; otherwise whichever card crosses the centre line.
let centreId = null, hoverId = null;
for (const card of cards) {
  card.addEventListener('pointerenter', () => { hoverId = card.dataset.plugin; showPlugin(hoverId); }, { signal });
  card.addEventListener('pointerleave', () => { hoverId = null; if (centreId) showPlugin(centreId); }, { signal });
  card.addEventListener('focusin', () => showPlugin(card.dataset.plugin), { signal });
}
const centreIO = new IntersectionObserver((entries) => {
  for (const e of entries) if (e.isIntersecting) { centreId = e.target.dataset.plugin; if (!hoverId) showPlugin(centreId); }
}, { rootMargin: '-48% 0px -48% 0px' });
cards.forEach((c) => centreIO.observe(c));
cleanups.push(() => centreIO.disconnect());

$('[data-hit]').addEventListener('click', () => { stage.hit = 1; stage.wake(); }, { signal });
const freezeBtn = $('[data-freeze]');
const syncFreeze = () => freezeBtn.setAttribute('aria-pressed', String(stage.frozen));
freezeBtn.addEventListener('click', () => { stage.frozen = !stage.frozen; syncFreeze(); stage.wake(); }, { signal });
syncFreeze();

// Live response to the OS reduce-motion switch (without overriding explicit user toggles).
reducedMotion.addEventListener('change', () => {
  stage.frozen = reducedMotion.matches; syncFreeze();
  stage.wake();
}, { signal });

/* ==========================================================================
   10. THE 3D STAGE — Three.js r187, WebGPURenderer (auto WebGL 2 fallback), TSL
   Loaded lazily so the catalog never waits on ~1 MB of GPU code, and isolated
   in try/catch so a missing GPU just leaves the CSS orb in place.
   No AudioContext anywhere: the page is silent by construction.
   ========================================================================== */
const backendEl = $('[data-backend]');
const enableScene = $('[data-enable-scene]');
async function enableStage() {
  if (disposed) return;
  enableScene.hidden = true;
  try { await startStage(); }
  catch (err) {
    if (disposed) return;
    root.dataset.stage = 'fallback';
    backendEl.textContent = '3D unavailable';
    console.warn('[stage]', err);
  }
}
async function hasHardwareGraphics() {
  try {
    const adapter = await navigator.gpu?.requestAdapter();
    if (adapter) {
      const info = adapter.info;
      return !/swiftshader|llvmpipe|software/i.test(
        [info?.architecture, info?.description, info?.device].join(' '));
    }
  } catch {
    // Fall through to WebGL2 probe
  }
  const probe = document.createElement('canvas').getContext('webgl2', {failIfMajorPerformanceCaveat:true});
  if (!probe) return false;
  const debug = probe.getExtension('WEBGL_debug_renderer_info');
  const name = debug ? probe.getParameter(debug.UNMASKED_RENDERER_WEBGL) : '';
  probe.getExtension('WEBGL_lose_context')?.loseContext();
  return !/swiftshader|llvmpipe|software/i.test(name);
}
enableScene.addEventListener('click', enableStage, { once: true, signal });
hasHardwareGraphics().catch(() => false).then(hardware => {
  if (disposed) return;
  if (hardware) return enableStage();
  root.dataset.stage = 'fallback';
  backendEl.textContent = '3D paused · software graphics';
  enableScene.hidden = false;
});

async function startStage() {
  const THREE = await import('three/webgpu');
  const TSL = await import('three/tsl');
  const { bloom } = await import('three/addons/tsl/display/BloomNode.js');
  const { RoomEnvironment } = await import('three/addons/environments/RoomEnvironment.js');
  const {
    Fn, uniform, float, vec3, vec4, mix, step, smoothstep, floor, fract, sin, cos, abs, max, clamp, pow, exp, dot,
    atan, hash, mx_noise_float, rotate, mod, instanceIndex, positionLocal, positionGeometry, normalLocal,
    normalView, positionViewDirection, pass, mrt, output, emissive, PI, TWO_PI,
  } = TSL;

  const canvas = $('#stage-canvas');
  const host = $('#stage');
  if (disposed || !canvas || !host) return;
  // ?webgl forces the WebGL 2 backend — handy for checking the fallback path.
  const forceWebGL = new URLSearchParams(location.search).has('webgl');
  const renderer = new THREE.WebGPURenderer({ canvas, antialias: true, alpha: true, forceWebGL });
  await renderer.init();
  if (disposed) { renderer.dispose(); return; }
  renderer.setPixelRatio(Math.min(devicePixelRatio, 1.75));
  renderer.setClearColor(0x000000, 0);
  renderer.toneMapping = THREE.NeutralToneMapping;

  const isWebGPU = Boolean(renderer.backend?.isWebGPUBackend);
  backendEl.textContent = `3D: ${isWebGPU ? 'WebGPU' : 'WebGL 2'}`;

  const scene = new THREE.Scene();
  const camera = new THREE.PerspectiveCamera(30, 1, 0.1, 100);
  const pmrem = new THREE.PMREMGenerator(renderer);
  const roomEnv = new RoomEnvironment();
  const envTarget = pmrem.fromScene(roomEnv, 0.04);
  scene.environment = envTarget.texture;
  roomEnv.dispose();
  pmrem.dispose();
  const key = new THREE.DirectionalLight(0xffffff, 1.6); key.position.set(3, 4, 5); scene.add(key);
  const rim = new THREE.DirectionalLight(0xffffff, 1.3); rim.position.set(-4, 1.5, -3); scene.add(rim);

  /* ---- shared uniforms (the macros, drag springs and clock) ----
     Photosensitivity rule for everything below: light never switches on/off.
     Rhythm shows as motion that glides and holds; brightness only rises and falls
     with that motion or with one slow swell every two bars (well under 1 Hz).
     WCAG 2.3.1 caps flashing at 3 per second — this stays far below it. */
  const U = {
    time: uniform(0),            // seconds of motion (stops while frozen)
    bars: uniform(0),            // musical time in bars at the active plugin's tempo (no jumps when tempo changes)
    slice: uniform(stage.macros.slice), formant: uniform(stage.macros.formant), open: uniform(stage.macros.open), chaos: uniform(stage.macros.chaos),
    twist: uniform(0), squash: uniform(0), hit: uniform(0), swell: uniform(0), seed: uniform(stage.seed),
  };
  const spectrum = Fn(([t]) => vec3(0.5).add(vec3(0.5).mul(cos(TWO_PI.mul(vec3(t).add(vec3(0.0, 0.33, 0.67)))))));
  const fresnel = Fn(() => pow(float(1).sub(abs(dot(normalView, positionViewDirection))), 3));
  const seedInt = floor(U.seed.mul(4096));

  /** Drag deformation shared by every form: twist about Y + volume-ish squash. */
  const deform = Fn(([p]) => {
    const q = rotate(p, vec3(0, U.twist.mul(p.y), 0));
    return q.mul(vec3(float(1).sub(U.squash.mul(0.45)), float(1).add(U.squash), float(1).sub(U.squash.mul(0.45))));
  });

  /** Wireframe → solid crystallisation mask driven by a presence uniform. */
  const crystal = (presence, seed) => {
    const n = mx_noise_float(positionGeometry.mul(2.4).add(seed)).mul(0.5).add(0.5);
    const thr = presence.mul(1.25).sub(0.12);
    const mask = step(n, thr);
    const edge = smoothstep(thr.sub(0.12), thr, n).mul(mask).mul(step(presence, 0.995));
    return { mask, edge };
  };

  /** Thin-film thickness (nm) varies across the surface and slides with the vowel → prismatic, not flat. */
  const filmThickness = (lo, hi) => mix(float(lo), float(hi),
    clamp(U.formant.mul(0.65).add(mx_noise_float(TSL.positionWorld.mul(1.4)).mul(0.35).add(0.2)), 0, 1));

  // Liquid chrome with a thin-film coat by default; forms override toward pearl or dark glass.
  const glassMaterial = (opts = {}) => new THREE.MeshPhysicalNodeMaterial({
    color: 0xe8e6f4, metalness: 0.92, roughness: 0.12, clearcoat: 1, clearcoatRoughness: 0.05,
    iridescence: 0.9, iridescenceIOR: 1.6, ...opts,
  });
  const wireMaterial = (glow, opacity) => {
    const m = new THREE.MeshStandardNodeMaterial({ wireframe: true, transparent: true, depthWrite: false, color: 0x000000, roughness: 1, metalness: 0 });
    m.colorNode = vec3(0);
    m.emissiveNode = glow.mul(opacity).mul(1.4);
    m.opacityNode = opacity;
    return m;
  };

  /** One form = a group + its presence springs + per-frame update. Forms are a small
      library shared by every plugin; a plugin only picks one and tints it. */
  const forms = {};
  function defineForm(name, build) {
    const solid = uniform(0), wire = uniform(0);
    const accent = uniform(new THREE.Color());
    const group = new THREE.Group();
    const wireOpacity = wire.mul(float(1).sub(solid.mul(0.86))).add(U.swell.mul(0.06).mul(solid));
    const update = build({ group, solid, wire, wireOpacity, accent }) ?? (() => {});
    group.visible = false;
    scene.add(group);
    forms[name] = { group, solid, wire, accent, update, s: 0, w: 0 };
  }

  /* ---- Monoliths: blocks of cuts. Once per bar the blocks glide to a new re-edit and hold. ---- */
  defineForm('monoliths', ({ group, solid, wireOpacity, accent }) => {
    const SL = 16, inner = 5, outer = 7, cols = inner + outer, count = cols * SL;
    const geo = new THREE.CylinderGeometry(1, 1, 1, 6, 1);
    const cutAt = (col, grp, phrase) => {
      const s = col.mul(97).add(grp.mul(13)).add(phrase.mul(7919)).add(seedInt);
      const cut = step(float(0.84).sub(U.chaos.mul(0.45)), hash(s));
      return {
        cut,
        slide: hash(s.add(1)).sub(0.5).mul(float(0.2).add(U.chaos.mul(0.5))).mul(cut),
        push: hash(s.add(2)).mul(float(0.04).add(U.chaos.mul(0.16))).mul(cut),
      };
    };
    // Shared per-instance maths for position and glow.
    const i = instanceIndex.toFloat();
    const col = floor(i.div(SL));
    const sl = i.sub(col.mul(SL));
    const ring = step(inner - 0.5, col);
    const k = col.sub(ring.mul(inner));
    const a = k.div(mix(float(inner), float(outer), ring)).mul(TWO_PI).add(ring.mul(PI.div(outer)));
    const gs = pow(float(2), floor(mix(float(3.99), float(0), U.slice)));   // slice density = block size
    const grp = floor(sl.div(gs));
    const phrase = floor(U.bars);
    const glide = smoothstep(0.0, 0.42, fract(U.bars));                       // move early in the bar, then hold
    const A0 = cutAt(col, grp, phrase), A1 = cutAt(col, grp, phrase.add(1));
    const cutAmt = mix(A0.cut, A1.cut, glide);
    const openK = mix(float(0.55), float(1.6), U.open);

    const pos = Fn(() => {
      const dir = vec3(cos(a), 0, sin(a));
      const tan = vec3(sin(a).negate(), 0, cos(a));
      const slide = mix(A0.slide, A1.slide, glide).mul(openK);
      const push = mix(A0.push, A1.push, glide).mul(openK).add(U.hit.mul(0.22));
      const p = positionLocal.add(tan.mul(slide)).add(dir.mul(push));
      // A slow, continuous orbit: 1/32 of a turn per bar, rings counter-rotating.
      const ang = U.bars.mul(TWO_PI.div(32)).mul(mix(float(1), float(-0.6), ring));
      return deform(rotate(p, vec3(0, ang, 0)));
    })();

    const { mask, edge } = crystal(solid, vec3(i.mul(1.31)));
    const mat = glassMaterial({ flatShading: true });
    mat.positionNode = pos;
    mat.opacityNode = mask; mat.alphaTest = 0.5;
    mat.iridescenceThicknessNode = filmThickness(240, 820);
    // Displaced blocks hold a soft glow that fades in and out with their glide — no flashes.
    mat.emissiveNode = accent.mul(cutAmt.mul(0.24).add(edge.mul(3)).add(U.hit.mul(0.8)));
    const mesh = new THREE.InstancedMesh(geo, mat, count);
    const wire = new THREE.InstancedMesh(geo, wireMaterial(accent, wireOpacity), count);
    wire.material.positionNode = pos;
    const m = new THREE.Matrix4(), q = new THREE.Quaternion(), s = new THREE.Vector3(), v = new THREE.Vector3(), Y = new THREE.Vector3(0, 1, 0);
    for (let c = 0; c < cols; c++) {
      const outerRing = c >= inner, n = outerRing ? outer : inner, kk = outerRing ? c - inner : c;
      const ang = kk / n * Math.PI * 2 + (outerRing ? Math.PI / outer : 0);
      const R = outerRing ? 1.3 : 0.55, H = outerRing ? 1.35 : 2.05, w = outerRing ? 0.15 : 0.2;
      for (let j = 0; j < SL; j++) {
        const h = H / SL;
        v.set(Math.cos(ang) * R, -H / 2 + (j + 0.5) * h, Math.sin(ang) * R);
        q.setFromAxisAngle(Y, -ang);
        s.set(w, h * 0.84, w);
        m.compose(v, q, s);
        mesh.setMatrixAt(c * SL + j, m);
      }
    }
    wire.instanceMatrix = mesh.instanceMatrix;          // one buffer, two draws
    group.add(mesh, wire);
    group.rotation.x = 0.22;
  });

  /* ---- Tube: a vowel-rippled tube around a ring that never moves ---- */
  defineForm('tube', ({ group, solid, wireOpacity, accent }) => {
    const amber = vec3(0.96, 0.62, 0.2);
    // Ripple counts per formant for a front vowel (i) and a back vowel (u); the vowel space blends them.
    const FRONT = [1.1, 9.2, 12, 14, 18], BACK = [1.3, 3.2, 9.4, 13, 18], AMP = [0.075, 0.055, 0.04, 0.03, 0.022];
    const ripple = Fn(() => {
      const u = atan(positionGeometry.y, positionGeometry.x);
      let r = float(0);
      for (let j = 0; j < 5; j++) {
        // Blend neighbouring integer ripple counts so the morph is smooth AND the ring stays seamless.
        const kf = mix(float(FRONT[j]), float(BACK[j]), U.formant);
        const k0 = floor(kf), w = fract(kf);
        const ph = U.time.mul(0.45 + j * 0.25);
        r = r.add(mix(sin(u.mul(k0).add(ph)), sin(u.mul(k0.add(1)).add(ph)), w).mul(AMP[j]));
      }
      r = r.mul(mix(float(0.55), float(1.7), U.open));                     // open vowels ripple wider
      r = r.add(sin(u.mul(floor(mix(float(16), float(40), U.slice))).sub(U.time.mul(1.2))).mul(0.018).mul(U.slice.add(0.2)));
      r = r.add(mx_noise_float(positionGeometry.mul(2.2).add(vec3(U.seed.mul(9), 0, U.time.mul(0.5)))).mul(U.chaos.mul(0.17)));
      return r.mul(float(1).add(U.hit.mul(0.9)));
    })();
    const pos = deform(positionLocal.add(normalLocal.mul(ripple)));
    const tubeGeo = new THREE.TorusGeometry(0.95, 0.22, 40, 260);
    const { mask, edge } = crystal(solid, vec3(0.0));
    const mat = glassMaterial({ color: 0xf3e9dc, metalness: 0.7, roughness: 0.14 });   // warm chrome-pearl
    mat.positionNode = pos;
    mat.opacityNode = mask; mat.alphaTest = 0.5;
    mat.iridescenceThicknessNode = filmThickness(300, 860);
    mat.emissiveNode = amber.mul(smoothstep(0.06, 0.16, ripple).mul(1.2)).add(accent.mul(edge.mul(3)));
    const tube = new THREE.Mesh(tubeGeo, mat);
    const tubeWire = new THREE.Mesh(tubeGeo, wireMaterial(amber, wireOpacity));
    tubeWire.material.positionNode = pos;
    // The still ring: no twist, no chaos, no ripple. It just glows, dead steady.
    const subGeo = new THREE.TorusGeometry(0.46, 0.03, 12, 160);
    const subMask = crystal(solid, vec3(3.0));
    const subMat = new THREE.MeshStandardNodeMaterial({ color: 0x0a0d12, roughness: 0.4, metalness: 0.2 });
    subMat.emissiveNode = accent.mul(1.8);
    subMat.opacityNode = subMask.mask; subMat.alphaTest = 0.5;
    const sub = new THREE.Mesh(subGeo, subMat);
    group.add(tube, tubeWire, sub);
    group.rotation.x = -1.05;
    return (dt, moving) => { if (moving) tube.rotation.z = tubeWire.rotation.z += dt * 0.1; };
  });

  /* ---- Rings: interlocking Euclidean rings; a soft light travels them, never strobes ---- */
  defineForm('rings', ({ group, solid, wireOpacity, accent }) => {
    const rings = [
      { n: 8, k: 3, r: 0.62, tilt: [0.95, 0.1, 0.2] },
      { n: 12, k: 5, r: 0.98, tilt: [-0.45, 0.65, 0] },
      { n: 16, k: 7, r: 1.34, tilt: [0.25, -0.3, 0.85] },
    ];
    const groups = rings.map(({ n, k, r, tilt }, ri) => {
      const g = new THREE.Group();
      g.rotation.set(...tilt);
      const ringGeo = new THREE.TorusGeometry(r, 0.022, 6, n);
      const { mask, edge } = crystal(solid, vec3(ri * 2.1));
      const ringMat = glassMaterial({ metalness: 0.85, roughness: 0.18, color: 0xdedcf0 });
      ringMat.positionNode = deform(positionLocal);
      ringMat.opacityNode = mask; ringMat.alphaTest = 0.5;
      ringMat.emissiveNode = accent.mul(edge.mul(3));
      const wire = new THREE.Mesh(ringGeo, wireMaterial(accent, wireOpacity));
      wire.material.positionNode = deform(positionLocal);
      // Nodes: onset = (s·k mod n) < k — the Bresenham form of a Euclidean rhythm.
      const nodeGeo = new THREE.IcosahedronGeometry(1, 0);
      const nodeMat = glassMaterial({ flatShading: true, color: 0xffffff });
      const s = instanceIndex.toFloat();
      const kEff = clamp(floor(mix(float(k * 0.5), float(Math.min(n, k * 1.7)), U.slice)), 1, n);
      const onset = step(mod(s.mul(kEff), float(n)), kEff.sub(0.5));
      // A continuous "playhead" goes round once every two bars; each node brightens smoothly as it passes.
      const head = fract(U.bars.mul(ri % 2 ? -0.5 : 0.5).add(ri * 0.13));
      const dist = abs(fract(s.div(n).sub(head).add(0.5)).sub(0.5)).mul(n);     // in steps, wrapped
      const glow = exp(dist.mul(dist).mul(-0.7)).mul(onset);
      nodeMat.positionNode = deform(positionLocal.add(normalLocal.mul(onset.mul(mix(float(0.015), float(0.045), U.open)).add(glow.mul(0.03)))));
      nodeMat.emissiveNode = accent.mul(glow.mul(1.4).add(onset.mul(0.25)).add(U.hit.mul(0.8)));
      const nodeMask = crystal(solid, vec3(ri * 5.0));
      nodeMat.opacityNode = nodeMask.mask; nodeMat.alphaTest = 0.5;
      const nodes = new THREE.InstancedMesh(nodeGeo, nodeMat, n);
      const m = new THREE.Matrix4();
      for (let j = 0; j < n; j++) {
        const ang = j / n * Math.PI * 2;
        m.makeScale(0.055, 0.055, 0.055).setPosition(Math.cos(ang) * r, Math.sin(ang) * r, 0);
        nodes.setMatrixAt(j, m);
      }
      g.add(new THREE.Mesh(ringGeo, ringMat), wire, nodes);
      group.add(g);
      return { g, tilt };
    });
    const core = new THREE.Mesh(new THREE.IcosahedronGeometry(0.2, 1), glassMaterial({ flatShading: true }));
    const coreMask = crystal(solid, vec3(9.0));
    core.material.opacityNode = coreMask.mask; core.material.alphaTest = 0.5;
    core.material.emissiveNode = accent.mul(U.swell.mul(0.35).add(U.hit.mul(1.2)));
    group.add(core);
    let spin = 0;
    return (dt, moving) => {
      if (moving) spin += dt;
      const open = 0.6 + stage.macros.open * 0.8;                          // open vowels splay the rings apart
      groups.forEach(({ g, tilt }, i) => {
        g.rotation.x = tilt[0] * open; g.rotation.y = tilt[1] * open + spin * 0.03 * (i + 1);
        g.rotation.z = tilt[2] + spin * (0.12 + i * 0.05) * (i % 2 ? -1 : 1);
      });
    };
  });

  /* ---- Prisms: an octahedron cluster with slow glints and a spectral fan ---- */
  defineForm('prisms', ({ group, solid, wireOpacity, accent }) => {
    const geo = new THREE.OctahedronGeometry(1, 0);
    let seedState = 7;
    const rand = () => ((seedState = (seedState * 16807) % 2147483647) / 2147483647);
    const base = [{ p: new THREE.Vector3(0, 0, 0), s: 0.6, r: new THREE.Euler(0.2, 0.4, 0) }];
    for (let i = 0; i < 8; i++) {
      const ang = i / 8 * Math.PI * 2 + rand() * 0.4, y = (rand() - 0.5) * 1.2, R = 0.75 + rand() * 0.3;
      base.push({ p: new THREE.Vector3(Math.cos(ang) * R, y, Math.sin(ang) * R), s: 0.18 + rand() * 0.2, r: new THREE.Euler(rand() * 3, rand() * 3, rand() * 3) });
    }
    const { mask, edge } = crystal(solid, vec3(instanceIndex.toFloat().mul(2.7)));
    const mat = new THREE.MeshPhysicalNodeMaterial({
      color: 0x1b1a26, metalness: 0, roughness: 0.03, clearcoat: 1, clearcoatRoughness: 0.02,
      iridescence: 1, iridescenceIOR: 2.0, ior: 2.3, specularIntensity: 1, flatShading: true,
    });
    // Each crystal glints on its own slow cycle (6–9 s): a rise and fall, never a blink.
    const ii = instanceIndex.toFloat();
    const glint = pow(sin(U.time.mul(float(0.7).add(hash(ii.add(seedInt)).mul(0.35))).add(ii.mul(2.4))).mul(0.5).add(0.5), 6);
    mat.positionNode = deform(positionLocal);
    mat.opacityNode = mask; mat.alphaTest = 0.5;
    mat.iridescenceThicknessNode = filmThickness(200, 900);
    mat.emissiveNode = spectrum(dot(normalView, positionViewDirection).mul(1.6).add(U.formant).add(U.time.mul(0.03)))
      .mul(fresnel().mul(0.7)).add(accent.mul(glint.mul(0.9).add(edge.mul(3)).add(U.hit.mul(0.8))));
    const mesh = new THREE.InstancedMesh(geo, mat, base.length);
    const wire = new THREE.InstancedMesh(geo, wireMaterial(accent, wireOpacity), base.length);
    wire.material.positionNode = deform(positionLocal);
    wire.instanceMatrix = mesh.instanceMatrix;
    // Spectral fan: thin rays, each a hue of the spectrum, breathing slowly.
    const RAYS = 11;
    const rayGeo = new THREE.BoxGeometry(1, 1, 1).translate(0, 0, 0.5);
    const rayMat = new THREE.MeshStandardNodeMaterial({ color: 0x000000, transparent: true, depthWrite: false });
    const ri = instanceIndex.toFloat();
    const shimmer = sin(U.time.mul(0.8).add(ri.mul(0.9))).mul(0.2).add(0.8);
    const along = float(1).sub(positionGeometry.z.add(0.5).mul(0.5).add(0.25));
    rayMat.colorNode = vec3(0);
    rayMat.emissiveNode = spectrum(ri.div(RAYS).mul(0.85)).mul(shimmer).mul(1.4);
    rayMat.opacityNode = solid.mul(along).mul(float(0.3).add(U.slice.mul(0.5)));
    rayMat.positionNode = deform(positionLocal);
    const rays = new THREE.InstancedMesh(rayGeo, rayMat, RAYS);
    const rm = new THREE.Matrix4(), rq = new THREE.Quaternion(), rs = new THREE.Vector3();
    for (let i = 0; i < RAYS; i++) {
      rq.setFromEuler(new THREE.Euler(-0.55 + i * 0.11, 1.15, 0));
      rs.set(0.012, 0.012, 3.2);
      rm.compose(new THREE.Vector3(0.2, 0, 0), rq, rs);
      rays.setMatrixAt(i, rm);
    }
    group.add(mesh, wire, rays);
    const m = new THREE.Matrix4(), q = new THREE.Quaternion(), sv = new THREE.Vector3(), pv = new THREE.Vector3(), e = new THREE.Euler();
    let t = 0;
    return (dt, moving) => {
      if (moving) t += dt;
      const spread = 0.85 + stage.macros.open * 0.55 + U.hit.value * 0.3;   // open vowels open the cluster
      base.forEach((b, i) => {
        pv.copy(b.p).multiplyScalar(spread); pv.y += Math.sin(t * 0.7 + i) * 0.04;
        e.set(b.r.x + t * 0.12 * (i % 3 - 1), b.r.y + t * 0.16, b.r.z);
        q.setFromEuler(e); sv.setScalar(b.s);
        m.compose(pv, q, sv); mesh.setMatrixAt(i, m);
      });
      mesh.instanceMatrix.needsUpdate = true;
      group.rotation.y += moving ? dt * 0.08 : 0;
    };
  });

  /* ---- Helix: a twisted torus knot around a locked sub-anchor ring (Torus Glide) ---- */
  defineForm('helix', ({ group, solid, wireOpacity, accent }) => {
    const cyan = vec3(0.28, 0.78, 0.96);
    const knotGeo = new THREE.TorusKnotGeometry(0.72, 0.16, 220, 32, 2, 3);
    const pos = deform(positionLocal);
    const { mask, edge } = crystal(solid, vec3(1.5));
    const mat = glassMaterial({ color: 0xe6f2f8, metalness: 0.74, roughness: 0.12 });
    mat.positionNode = pos;
    mat.opacityNode = mask; mat.alphaTest = 0.5;
    mat.iridescenceThicknessNode = filmThickness(260, 880);
    mat.emissiveNode = cyan.mul(0.22).add(accent.mul(edge.mul(3.2)));
    const knot = new THREE.Mesh(knotGeo, mat);
    const knotWire = new THREE.Mesh(knotGeo, wireMaterial(cyan, wireOpacity));
    knotWire.material.positionNode = pos;
    const anchorGeo = new THREE.TorusGeometry(0.44, 0.028, 12, 160);
    const anchorMask = crystal(solid, vec3(3.2));
    const anchorMat = new THREE.MeshStandardNodeMaterial({ color: 0x0a0d12, roughness: 0.35, metalness: 0.2 });
    anchorMat.emissiveNode = vec3(0.30, 0.88, 0.65).mul(1.7);
    anchorMat.opacityNode = anchorMask.mask; anchorMat.alphaTest = 0.5;
    const anchor = new THREE.Mesh(anchorGeo, anchorMat);
    anchor.rotation.x = Math.PI * 0.5;
    group.add(knot, knotWire, anchor);
    return (dt, moving) => {
      if (moving) {
        knot.rotation.y = knotWire.rotation.y += dt * 0.14;
        knot.rotation.x = knotWire.rotation.x += dt * 0.05;
      }
    };
  });

  /* ---- Membrane: tensioned Chladni drumhead with standing-wave Bessel ripples (Strike Body) ---- */
  defineForm('membrane', ({ group, solid, wireOpacity, accent }) => {
    const amber = vec3(1.0, 0.42, 0.16);
    const cyan = vec3(0.11, 0.91, 1.0);
    const headGeo = new THREE.CylinderGeometry(1.18, 1.18, 0.12, 96, 1, false);
    const chladni = Fn(() => {
      const r = TSL.length(positionGeometry.xz);
      const a = atan(positionGeometry.z, positionGeometry.x);
      const kr = mix(float(5.5), float(13.5), U.formant);
      const m = floor(mix(float(2.0), float(6.0), U.slice));
      const wave = cos(r.mul(kr).sub(U.time.mul(1.8))).mul(cos(a.mul(m).add(U.time.mul(0.35))));
      const shock = sin(r.mul(14.0).sub(U.hit.mul(10.0))).mul(U.hit).mul(0.28);
      const rimFade = smoothstep(float(1.18), float(0.15), r);
      const disp = wave.mul(mix(float(0.05), float(0.19), U.open)).add(shock).mul(rimFade);
      return disp;
    })();
    const pos = deform(positionLocal.add(vec3(0, chladni, 0)));
    const { mask, edge } = crystal(solid, vec3(2.4));
    const mat = glassMaterial({ color: 0x1a1e2e, metalness: 0.86, roughness: 0.12 });
    mat.positionNode = pos;
    mat.opacityNode = mask; mat.alphaTest = 0.5;
    mat.iridescenceThicknessNode = filmThickness(220, 840);
    mat.emissiveNode = amber.mul(abs(chladni).mul(4.5)).add(accent.mul(edge.mul(3.2))).add(cyan.mul(U.hit.mul(0.9)));
    const head = new THREE.Mesh(headGeo, mat);
    const headWire = new THREE.Mesh(headGeo, wireMaterial(amber, wireOpacity));
    headWire.material.positionNode = pos;
    group.add(head, headWire);

    const ringDefs = [
      { r: 1.24, tube: 0.032, col: amber },
      { r: 0.80, tube: 0.018, col: vec3(1.0, 0.74, 0.18) },
      { r: 0.38, tube: 0.020, col: cyan },
    ];
    for (let idx = 0; idx < ringDefs.length; idx++) {
      const rd = ringDefs[idx];
      const rg = new THREE.TorusGeometry(rd.r, rd.tube, 14, 128);
      const rm = new THREE.MeshStandardNodeMaterial({ color: 0x0a0d14, roughness: 0.3, metalness: 0.3 });
      const rMask = crystal(solid, vec3(4.0 + idx * 1.7));
      rm.emissiveNode = rd.col.mul(float(1.3).add(U.hit.mul(1.2)));
      rm.opacityNode = rMask.mask; rm.alphaTest = 0.5;
      const ringMesh = new THREE.Mesh(rg, rm);
      ringMesh.rotation.x = Math.PI * 0.5;
      ringMesh.position.y = 0.08 + idx * 0.03;
      group.add(ringMesh);
    }
    group.rotation.x = 0.52;
    return (dt, moving) => {
      if (moving) group.rotation.y += dt * 0.12;
    };
  });

  /* ---- floor rings: a faint polar grid (the gallery plinth) ---- */
  const gridColor = uniform(new THREE.Color());
  {
    const pts = [];
    for (let r = 1; r <= 4; r++) for (let i = 0; i < 128; i++) {
      const a0 = i / 128 * Math.PI * 2, a1 = (i + 1) / 128 * Math.PI * 2, R = r * 0.55;
      pts.push(Math.cos(a0) * R, 0, Math.sin(a0) * R, Math.cos(a1) * R, 0, Math.sin(a1) * R);
    }
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.Float32BufferAttribute(pts, 3));
    const lm = new THREE.LineBasicNodeMaterial({ transparent: true, depthWrite: false });
    lm.colorNode = gridColor;
    lm.opacityNode = float(0.22).mul(float(1).sub(TSL.length(positionGeometry.xz).div(2.4)));
    const floorLines = new THREE.LineSegments(g, lm);
    floorLines.position.y = -1.55;
    scene.add(floorLines);
  }

  /* ---- render pipeline: MRT { output, emissive } → bloom only the emissive ---- */
  const scenePass = pass(scene, camera);
  const mrtNode = mrt({ output, emissive: vec4(emissive, output.a) });
  mrtNode.setBlendMode('emissive', new THREE.BlendMode(THREE.NormalBlending));
  scenePass.setMRT(mrtNode);
  scenePass.getTexture('emissive').type = THREE.UnsignedByteType;   // bandwidth: 8-bit is plenty for glow
  const colorTex = scenePass.getTextureNode('output');
  const glow = bloom(scenePass.getTextureNode('emissive'), 1.1, 0.55, 0);
  const pipeline = new THREE.RenderPipeline(renderer);
  // Keep the canvas transparent: glow carries its own (premultiplied) alpha,
  // so the CSS page colour shows through wherever there's no sculpture.
  pipeline.outputNode = Fn(() => {
    const g = glow;
    const a = max(colorTex.a, clamp(max(g.r, max(g.g, g.b)), 0, 1));
    return vec4(colorTex.rgb.add(g.rgb), a);
  })();

  /* ---- theme ---- */
  const dark = matchMedia('(prefers-color-scheme: dark)');
  const applyTheme = () => {
    glow.strength.value = dark.matches ? 1.15 : 0.55;
    gridColor.value.set(dark.matches ? 0xc9c6e0 : 0x2b2840);
    scene.environmentIntensity = dark.matches ? 0.85 : 1.05;
    host.style.setProperty('--stage-light', dark.matches ? '24%' : '30%');
    stage.wake();
  };
  dark.addEventListener('change', applyTheme, { signal });

  /* ---- sizing ---- */
  let W = 1, H = 1;
  const stageRO = new ResizeObserver(([entry]) => {
    const box = entry.contentBoxSize?.[0];
    W = Math.max(1, Math.round(box ? box.inlineSize : host.clientWidth));
    H = Math.max(1, Math.round(box ? box.blockSize : host.clientHeight));
    renderer.setSize(W, H, false);
    camera.aspect = W / H;
    camera.updateProjectionMatrix();
    stage.wake();
  });
  stageRO.observe(host);

  /* ---- input: pointer parallax, drag-to-twist with spring return, tap-to-hit ---- */
  const pointer = { x: 0, y: 0, sx: 0, sy: 0 };
  addEventListener('pointermove', (e) => {
    pointer.x = e.clientX / innerWidth * 2 - 1;
    pointer.y = e.clientY / innerHeight * 2 - 1;
    if (!stage.frozen) stage.wake();
  }, { passive: true, signal });

  const spring = { twist: 0, tv: 0, squash: 0, sv: 0, tTarget: 0, sTarget: 0, dragging: false };
  for (const zone of $$('[data-grab]')) {
    let start = null;
    zone.addEventListener('pointerdown', (e) => {
      start = { x: e.clientX, y: e.clientY, t: performance.now(), id: e.pointerId };
      spring.dragging = true;
      stage.wake();
    }, { signal });
    zone.addEventListener('pointermove', (e) => {
      if (!start || e.pointerId !== start.id) return;
      const dx = e.clientX - start.x, dy = e.clientY - start.y;
      if (Math.abs(dx) > 6 && !zone.hasPointerCapture(e.pointerId)) zone.setPointerCapture(e.pointerId);
      spring.tTarget = Math.max(-2.6, Math.min(2.6, dx / 140));
      spring.sTarget = e.pointerType === 'touch' ? 0 : Math.max(-0.35, Math.min(0.35, -dy / 600));
      stage.wake();
    }, { signal });
    const end = (e) => {
      if (!start || e.pointerId !== start.id) return;
      const moved = Math.hypot(e.clientX - start.x, e.clientY - start.y);
      if (moved < 6 && performance.now() - start.t < 400) stage.hit = 1;    // a tap is a hit
      start = null; spring.dragging = false; spring.tTarget = 0; spring.sTarget = 0;
      stage.wake();
    };
    zone.addEventListener('pointerup', end, { signal });
    zone.addEventListener('pointercancel', end, { signal });
  }

  /* ---- scroll choreography: each section has a "shot"; the camera blends between them ---- */
  // The CSS layout decides where the sculpture sits and how big it is: each
  // shot names an anchor element, and the camera frames the sculpture inside
  // that element's rect (via setViewOffset + distance). No magic numbers per breakpoint.
  const SCULPT = 3.0;                       // world-space extent of the largest preset
  const shots = [
    // Hero: frame the whole right column (drag area + dock); the dock's frosted glass sits over the sculpture's base.
    { el: $('.hero'),    anchor: [$('.hero__grab'), $('.hero__dock')], zoom: 1.32, az: 0.0, elv: 0.14 },
    { el: $('#plugins'), anchor: [$('[data-anchor="catalog"]')],       zoom: 1.15, az: 0.65, elv: 0.3 },
    { el: $('#notes'),   anchor: [$('[data-anchor="notes"]')],         zoom: 1.35, az: 1.5, elv: 0.75 },
  ];
  const cam = { dist: 8, az: 0, elv: 0.12, x: 0, y: 0 };
  const place = (shot) => {
    // On wide screens the hero frames grab area + dock together; stacked layouts frame the grab area alone.
    const els = W >= 1024 ? shot.anchor : shot.anchor.slice(0, 1);
    const rects = els.filter(Boolean).map((el) => el.getBoundingClientRect()).filter((r) => r.width);
    if (!rects.length) return { x: W / 2, y: H / 2, size: Math.min(W, H) * 0.85 };   // narrow screens: centred, behind content
    // Centre on the first anchor; size to the union of all of them.
    const [first] = rects;
    const left = Math.min(...rects.map((r) => r.left)), right = Math.max(...rects.map((r) => r.right));
    const top = Math.min(...rects.map((r) => r.top)), bottom = Math.max(...rects.map((r) => r.bottom));
    const cy = first.top + first.height * (rects.length > 1 ? 0.62 : 0.5);
    return { x: first.left + first.width / 2, y: Math.min(H * 0.7, Math.max(H * 0.2, cy)), size: Math.min(right - left, bottom - top) };
  };
  const shotTarget = () => {
    const mid = innerHeight * 0.5;
    let i = 0;
    for (let k = 0; k < shots.length; k++) if (shots[k].el.getBoundingClientRect().top <= mid) i = k;
    const a = shots[i], b = shots[Math.min(i + 1, shots.length - 1)];
    const ra = a.el.getBoundingClientRect(), rb = b.el.getBoundingClientRect();
    const t = a === b ? 0 : THREE.MathUtils.smootherstep(Math.min(1, Math.max(0, (mid - ra.top) / Math.max(1, rb.top - ra.top))), 0, 1);
    const pa = place(a), pb = place(b);
    const lerp = THREE.MathUtils.lerp;
    const size = Math.max(140, lerp(pa.size, pb.size, t));
    const dist = (SCULPT * H) / (size * 2 * Math.tan(THREE.MathUtils.degToRad(camera.fov / 2))) * lerp(a.zoom, b.zoom, t);
    return { dist, az: lerp(a.az, b.az, t), elv: lerp(a.elv, b.elv, t), x: lerp(pa.x, pb.x, t), y: lerp(pa.y, pb.y, t) };
  };
  addEventListener('scroll', () => stage.wake(), { passive: true, signal });

  /** OKLCH → linear sRGB (Björn Ottosson's OKLab matrices), gamut-clipped. Same
      colour space as the CSS tokens, so the sculpture and the UI agree on a hue. */
  const tmpColor = new THREE.Color();
  function oklchToLinear(L, C, hDeg, out) {
    const h = hDeg * Math.PI / 180, a = C * Math.cos(h), b = C * Math.sin(h);
    const l = (L + 0.3963377774 * a + 0.2158037573 * b) ** 3;
    const m = (L - 0.1055613458 * a - 0.0638541728 * b) ** 3;
    const s = (L - 0.0894841775 * a - 1.2914855480 * b) ** 3;
    const clip = (v) => Math.min(1, Math.max(0, v));
    return out.setRGB(
      clip(4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s),
      clip(-1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s),
      clip(-0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s),
      THREE.LinearSRGBColorSpace,
    );
  }

  /* ---- frame loop with pause/resume ---- */
  let running = false, visibleStage = true, last = performance.now(), simTime = 0, idleAz = 0, awakeUntil = 0, firstFrame = true;
  const lightPos = { x: '', y: '' };
  const damp = (cur, target, rate, dt) => cur + (target - cur) * (1 - Math.exp(-rate * dt));

  function frame() {
    const now = performance.now();
    const dt = Math.min(0.05, (now - last) / 1000);
    last = now;
    const moving = !stage.frozen;
    if (moving) simTime += dt;

    // Macros (smoothed so slider jumps glide).
    for (const k of ['slice', 'formant', 'open', 'chaos']) U[k].value = damp(U[k].value, stage.macros[k], 10, dt);
    U.seed.value = stage.seed;
    U.time.value = simTime;
    // Musical clock in bars, integrated so a tempo change (another plugin hovered) never jumps.
    if (moving) U.bars.value += dt * (stage.bpm || 120) / 240;
    // One slow swell every two bars — the only "beat" light, and well under 1 Hz.
    U.swell.value = moving ? 0.5 - 0.5 * Math.cos(Math.PI * U.bars.value) : 0;
    stage.hit = Math.max(0, stage.hit - dt * 3.2);
    U.hit.value = Math.pow(stage.hit, 2);

    // Damped springs for drag twist / squash.
    spring.tv += ((spring.tTarget - spring.twist) * 38 - spring.tv * 8.5) * dt;
    spring.twist += spring.tv * dt;
    spring.sv += ((spring.sTarget - spring.squash) * 46 - spring.sv * 9) * dt;
    spring.squash += spring.sv * dt;
    U.twist.value = spring.twist; U.squash.value = spring.squash;

    // Presets: the incoming one draws its wireframe first, then crystallises solid.
    let settled = Math.abs(spring.tv) < 1e-3 && Math.abs(spring.twist - spring.tTarget) < 1e-3 && stage.hit === 0;
    for (const [name, P] of Object.entries(forms)) {
      const on = name === stage.form;
      if (reducedMotion.matches || firstFrame) { P.w = on ? 1 : 0; P.s = on ? 1 : 0; }
      else {
        P.w = damp(P.w, on ? 1 : 0, on ? 7 : 5, dt);
        P.s = damp(P.s, on && P.w > 0.55 ? 1 : 0, on ? 2.4 : 5, dt);
      }
      P.wire.value = P.w; P.solid.value = P.s;
      P.group.visible = P.w > 0.003 || P.s > 0.003;
      P.group.scale.setScalar(0.9 + 0.1 * P.s);
      if (P.group.visible) P.update(dt, moving);
      settled &&= Math.abs(P.w - (on ? 1 : 0)) < 0.002 && Math.abs(P.s - (on ? 1 : 0)) < 0.002;
    }
    const accentColor = oklchToLinear(0.78, 0.16, stage.hue, tmpColor);
    for (const P of Object.values(forms)) P.accent.value.lerp(accentColor, 1 - Math.exp(-6 * dt));
    rim.color.lerp(accentColor, 1 - Math.exp(-4 * dt));

    // Camera: scroll shot + pointer parallax + idle drift (motion only).
    const target = shotTarget();
    const motionOK = moving && !reducedMotion.matches;
    pointer.sx = damp(pointer.sx, motionOK ? pointer.x : 0, 4, dt);
    pointer.sy = damp(pointer.sy, motionOK ? pointer.y : 0, 4, dt);
    if (motionOK) idleAz += dt * 0.05;
    const r = reducedMotion.matches || firstFrame ? 1 : 1 - Math.exp(-5 * dt);
    cam.dist += (target.dist - cam.dist) * r; cam.az += (target.az - cam.az) * r; cam.elv += (target.elv - cam.elv) * r;
    cam.x += (target.x - cam.x) * r; cam.y += (target.y - cam.y) * r;
    const az = cam.az + idleAz + pointer.sx * 0.18, elv = cam.elv + pointer.sy * 0.08;
    camera.position.set(Math.sin(az) * Math.cos(elv) * cam.dist, Math.sin(elv) * cam.dist, Math.cos(az) * Math.cos(elv) * cam.dist);
    camera.lookAt(0, 0, 0);
    camera.setViewOffset(W, H, W / 2 - cam.x, H / 2 - cam.y, W, H);
    // Only touch style when the light pool actually moves (a style write per frame costs a recalc).
    const lx = `${(cam.x / W * 100).toFixed(1)}%`, ly = `${(cam.y / H * 100).toFixed(1)}%`;
    if (lx !== lightPos.x) host.style.setProperty('--stage-x', (lightPos.x = lx));
    if (ly !== lightPos.y) host.style.setProperty('--stage-y', (lightPos.y = ly));
    settled &&= Math.abs(target.x - cam.x) < 0.5 && Math.abs(target.dist - cam.dist) < 0.01;

    pipeline.render();
    if (firstFrame) { firstFrame = false; root.dataset.stage = 'live'; }

    // Reduced motion / frozen: render on demand, then stop the loop when everything settles.
    if (!moving && settled && now > awakeUntil) setRunning(false);
  }

  function setRunning(on) {
    on = on && visibleStage && !document.hidden;
    if (on === running) return;
    running = on;
    if (on) last = performance.now();
    renderer.setAnimationLoop(on ? frame : null);
    // r187: setAnimationLoop(null) only clears the callback — the renderer's internal
    // requestAnimationFrame keeps ticking for node-frame bookkeeping, and there is no
    // public stop. Halt it too, so a paused stage costs zero GPU work AND zero wake-ups.
    // (setAnimationLoop(frame) restarts it.) Guarded in case the private field moves.
    if (!on) renderer._animation?.stop?.();
  }
  stage.wake = () => { awakeUntil = performance.now() + 400; setRunning(true); };

  // Pause when the stage can't be seen or the tab is hidden.
  // • contentvisibilityautostatechange: the platform's own "skipped" signal for this subtree.
  // • A zero-margin IntersectionObserver as well: content-visibility keeps elements
  //   "relevant" within a generous margin (~half a viewport in Chromium), and the stage
  //   should stop drawing the moment its last pixel leaves the screen.
  let cvRelevant = true, onScreen = true;
  const updateVisibility = () => { visibleStage = cvRelevant && onScreen; visibleStage ? stage.wake() : setRunning(false); };
  if ('contentVisibility' in root.style) {
    host.addEventListener('contentvisibilityautostatechange', (e) => { cvRelevant = !e.skipped; updateVisibility(); }, { signal });
  }
  const stageIO = new IntersectionObserver(([e]) => { onScreen = e.isIntersecting; updateVisibility(); });
  stageIO.observe(host);
  document.addEventListener('visibilitychange', () => (document.hidden ? setRunning(false) : stage.wake()), { signal });

  canvas.addEventListener('webglcontextlost', (e) => {
    e.preventDefault();
    setRunning(false);
    root.dataset.stage = 'fallback';
    backendEl.textContent = '3D paused · context lost';
  }, { signal });

  stageCleanup = () => {
    setRunning(false);
    stageRO.disconnect();
    stageIO.disconnect();
    scene.traverse((obj) => {
      obj.geometry?.dispose?.();
      if (Array.isArray(obj.material)) obj.material.forEach((m) => m?.dispose?.());
      else obj.material?.dispose?.();
    });
    envTarget.dispose();
    renderer.dispose();
  };

  // Compile every preset's pipelines up front so switching never stutters.
  try {
    for (const P of Object.values(forms)) P.group.visible = true;
    await renderer.compileAsync(scene, camera);
    if (disposed) { stageCleanup(); stageCleanup = null; return; }
    applyTheme();
    stage.wake();
  } catch (err) {
    stageCleanup?.();
    stageCleanup = null;
    throw err;
  }
}
/* ==========================================================================
   11. HOBERMAN CURSOR — a little expanding-linkage sphere that trails the pointer
   Decorative only: the native cursor stays, the canvas ignores pointer events and
   is hidden from assistive tech. Mouse/pen only, off under reduced motion and
   forced colours, and it sleeps (no rAF) whenever it has nothing left to settle.
   ========================================================================== */
cleanups.push(startHoberman());

function startHoberman() {
  const fine = matchMedia('(hover: hover) and (pointer: fine)');
  const forced = matchMedia('(forced-colors: active)');
  let toy = null;
  const sync = () => {
    const want = fine.matches && !reducedMotion.matches && !forced.matches;
    if (want && !toy) toy = createHoberman();
    else if (!want && toy) { toy.destroy(); toy = null; }
  };
  [fine, forced, reducedMotion].forEach((mq) => mq.addEventListener('change', sync, { signal }));
  sync();
  return () => { toy?.destroy(); toy = null; };
}

return function cleanupSculpture() {
  disposed = true;
  ac.abort();
  stageCleanup?.();
  stageCleanup = null;
  for (const fn of cleanups) fn();
  delete root.dataset.stage;
};
}

function createHoberman() {
  const SIZE = 180, HALF = SIZE / 2, N = 10;          // canvas px; scissor units per great circle
  const canvas = Object.assign(document.createElement('canvas'), { className: 'hoberman' });
  canvas.setAttribute('aria-hidden', 'true');
  const dpr = Math.min(2, devicePixelRatio || 1);
  canvas.width = canvas.height = Math.round(SIZE * dpr);
  document.body.append(canvas);
  const ctx = canvas.getContext('2d');
  ctx.scale(dpr, dpr);
  ctx.lineCap = 'round';

  // Six great circles whose normals sit on the icosahedron's five-fold axes — the real toy's layout.
  const PHI = (1 + Math.sqrt(5)) / 2;
  const norm = (v) => { const l = Math.hypot(...v); return v.map((c) => c / l); };
  const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
  const rings = [[0, 1, PHI], [0, 1, -PHI], [1, PHI, 0], [1, -PHI, 0], [PHI, 0, 1], [-PHI, 0, 1]].map((axis) => {
    const n = norm(axis);
    const u = norm(cross(n, Math.abs(n[0]) < 0.9 ? [1, 0, 0] : [0, 1, 0]));
    const v = cross(n, u);
    return Array.from({ length: N }, (_, i) => {
      const t = (i / N) * Math.PI * 2;
      return [0, 1, 2].map((k) => u[k] * Math.cos(t) + v[k] * Math.sin(t));
    });
  });

  const CLICKABLE = 'a[href], button:not(:disabled), summary, label, [role="button"], input[type="range"], select';
  const HIDE_OVER = 'input[type="search"], input[type="text"], textarea, [data-vowel-pad], .fg-canvas-box--interactive, dialog';
  const dark = matchMedia('(prefers-color-scheme: dark)');
  const s = {
    x: innerWidth / 2, y: innerHeight / 2, vx: 0, vy: 0, px: innerWidth / 2, py: innerHeight / 2,
    R: 10, rv: 0, open: 0.1, ov: 0, ax: 0.5, ay: 0, az: 0,
    hue: 285, alpha: 0, squeeze: 1, target: null, mode: 'free', seen: false, lastMove: 0,
  };
  let raf = 0, last = performance.now();

  const hueOf = (el) => {
    const scope = el?.closest?.('[style*="--hue"]');
    const h = scope && parseFloat(getComputedStyle(scope).getPropertyValue('--hue'));
    return Number.isFinite(h) ? h : 285;
  };
  const onMove = (e) => {
    if (e.pointerType === 'touch') return;
    if (s.seen && e.clientX === s.px && e.clientY === s.py) return;   // hover refreshes at a parked cursor aren't movement
    s.px = e.clientX; s.py = e.clientY; s.lastMove = performance.now();
    if (!s.seen) { s.x = s.px; s.y = s.py; s.seen = true; }
    wake();
  };
  const onOver = (e) => {
    const el = e.target;
    if (el.closest?.(HIDE_OVER)) { s.mode = 'hidden'; s.target = null; }
    else if (el.closest?.('[data-grab]')) { s.mode = 'grab'; s.target = null; }
    else {
      const hit = el.closest?.(CLICKABLE);
      s.mode = hit ? 'over' : 'free';
      s.target = hit || null;
    }
    s.hueTarget = hueOf(s.target || el);
    wake();
  };
  const onDown = () => { s.squeeze = 0.55; wake(); };
  const onUp = () => { s.squeeze = 1; wake(); };
  const onLeave = () => { s.mode = 'hidden'; wake(); };
  addEventListener('pointermove', onMove, { passive: true });
  addEventListener('pointerover', onOver, { passive: true });
  addEventListener('pointerdown', onDown, { passive: true });
  addEventListener('pointerup', onUp, { passive: true });
  document.documentElement.addEventListener('pointerleave', onLeave);
  addEventListener('blur', onLeave);

  function wake() { if (!raf && !document.hidden) { last = performance.now(); raf = requestAnimationFrame(tick); } }

  function tick(now) {
    raf = 0;
    const dt = Math.min(0.04, (now - last) / 1000);
    last = now;

    // Where to be, how big, how open.
    let tx = s.px, ty = s.py, RT = 10, openT = 0.12;
    if (s.mode === 'over' && s.target?.isConnected) {
      const r = s.target.getBoundingClientRect();
      if (r.width < 340 && r.height < 140) {                       // magnetise toward small controls
        tx += (r.left + r.width / 2 - tx) * 0.28;
        ty += (r.top + r.height / 2 - ty) * 0.28;
      }
      RT = Math.min(48, Math.max(22, Math.min(r.width, r.height) * 0.62 + 6));
      openT = 1;
    } else if (s.mode === 'grab') { RT = 56; openT = 0.85; }
    // Over a control it stays a little translucent, so the control's own label always reads first.
    const alphaT = s.seen && s.mode !== 'hidden' ? (s.mode === 'over' ? 0.7 : 1) : 0;

    // Springs: a little overshoot so the opening reads as mechanical, not a fade.
    s.vx += ((tx - s.x) * 210 - s.vx * 24) * dt; s.x += s.vx * dt;
    s.vy += ((ty - s.y) * 210 - s.vy * 24) * dt; s.y += s.vy * dt;
    s.rv += ((RT * s.squeeze - s.R) * 170 - s.rv * 15) * dt; s.R += s.rv * dt;
    s.ov += ((openT * s.squeeze - s.open) * 120 - s.ov * 13) * dt; s.open += s.ov * dt;
    s.alpha += (alphaT - s.alpha) * (1 - Math.exp(-10 * dt));
    s.hue += (((s.hueTarget ?? 285) - s.hue + 540) % 360 - 180) * (1 - Math.exp(-6 * dt));
    const speed = Math.hypot(s.vx, s.vy);
    s.ay += dt * (0.35 + Math.min(speed, 2500) * 0.0012 + (s.mode === 'grab' ? 0.8 : 0));
    s.ax += ((0.45 + s.vy * 0.0008) - s.ax) * (1 - Math.exp(-4 * dt));
    s.az += ((-s.vx * 0.0006) - s.az) * (1 - Math.exp(-4 * dt));

    draw();
    canvas.style.transform = `translate3d(${(s.x - HALF).toFixed(1)}px, ${(s.y - HALF).toFixed(1)}px, 0)`;

    const settled = speed < 2 && Math.abs(s.rv) < 0.05 && Math.abs(s.ov) < 0.01 && Math.abs(s.alpha - alphaT) < 0.01;
    if (!(settled && now - s.lastMove > 1400)) raf = requestAnimationFrame(tick);   // sleep when idle
  }

  function draw() {
    ctx.clearRect(0, 0, SIZE, SIZE);
    if (s.alpha < 0.01) return;
    const open = Math.min(1.2, Math.max(0, s.open));
    const R = Math.max(4, s.R);
    const d = R * (0.5 - 0.42 * Math.min(1, open));                // collapsed: chunky scissors; open: a thin lattice
    const [cx, sx] = [Math.cos(s.ax), Math.sin(s.ax)], [cy, sy] = [Math.cos(s.ay), Math.sin(s.ay)], [cz, sz] = [Math.cos(s.az), Math.sin(s.az)];
    const rot = ([x, y, z]) => {
      let X = x * cy + z * sy, Z = -x * sy + z * cy;                // yaw
      let Y = y * cx - Z * sx; Z = y * sx + Z * cx;                  // pitch
      return [X * cz - Y * sz, X * sz + Y * cz, Z];                 // roll
    };
    const F = 260;
    const proj = (v, r) => { const [x, y, z] = rot(v); const k = F / (F - z * r); return [HALF + x * r * k, HALF + y * r * k, z]; };
    const L = dark.matches ? 0.82 : 0.55;
    ctx.lineWidth = 1.15;
    rings.forEach((ring, k) => {
      const hue = (s.hue + k * 24) % 360;
      for (let i = 0; i < N; i++) {
        const a = ring[i], b = ring[(i + 1) % N];
        const p1 = proj(a, R + d), p2 = proj(b, R - d), p3 = proj(a, R - d), p4 = proj(b, R + d);
        const depth = (p1[2] + p2[2] + 1) / 2;                       // 0 = back, 1 = front
        ctx.strokeStyle = `oklch(${L} 0.15 ${hue.toFixed(0)} / ${(s.alpha * (0.18 + 0.82 * depth)).toFixed(3)})`;
        ctx.beginPath();
        ctx.moveTo(p1[0], p1[1]); ctx.lineTo(p2[0], p2[1]);
        ctx.moveTo(p3[0], p3[1]); ctx.lineTo(p4[0], p4[1]);
        ctx.stroke();
        if (depth > 0.55) { ctx.fillStyle = ctx.strokeStyle; ctx.fillRect(p1[0] - 1, p1[1] - 1, 2, 2); }   // hub joints
      }
    });
  }

  return {
    destroy() {
      cancelAnimationFrame(raf);
      removeEventListener('pointermove', onMove);
      removeEventListener('pointerover', onOver);
      removeEventListener('pointerdown', onDown);
      removeEventListener('pointerup', onUp);
      document.documentElement.removeEventListener('pointerleave', onLeave);
      removeEventListener('blur', onLeave);
      canvas.remove();
    },
  };
}
