// Lazy Three.js WebGPU / WebGL2 stage with MRT bloom and cached anchor-rect camera choreography.
import { registerSculptureForms } from "./forms";

export async function hasHardwareGraphics() {
  try {
    const adapter = await navigator.gpu?.requestAdapter();
    if (adapter) {
      const info = adapter.info;
      return !/swiftshader|llvmpipe|software/i.test(
        [info?.architecture, info?.description, info?.device].join(" "),
      );
    }
  } catch {
    // Fall through to WebGL2 probe
  }
  const probe = document.createElement("canvas").getContext("webgl2", { failIfMajorPerformanceCaveat: true });
  if (!probe) return false;
  const debug = probe.getExtension("WEBGL_debug_renderer_info");
  const name = debug ? probe.getParameter(debug.UNMASKED_RENDERER_WEBGL) : "";
  probe.getExtension("WEBGL_lose_context")?.loseContext();
  return !/swiftshader|llvmpipe|software/i.test(name);
}

export async function startStage({ root, stage, reducedMotion, backendEl, signal, isDisposed }) {
  const $ = (selector, scope = document) => scope.querySelector(selector);
  const $$ = (selector, scope = document) => [...scope.querySelectorAll(selector)];

  const THREE = await import("three/webgpu");
  const TSL = await import("three/tsl");
  const { bloom } = await import("three/addons/tsl/display/BloomNode.js");
  const { RoomEnvironment } = await import("three/addons/environments/RoomEnvironment.js");
  const {
    Fn,
    uniform,
    float,
    vec3,
    vec4,
    mix,
    step,
    smoothstep,
    floor,
    cos,
    abs,
    max,
    clamp,
    pow,
    dot,
    mx_noise_float,
    rotate,
    positionGeometry,
    normalView,
    positionViewDirection,
    pass,
    mrt,
    output,
    emissive,
    TWO_PI,
  } = TSL;

  const canvas = $("#stage-canvas");
  const host = $("#stage");
  if (isDisposed() || !canvas || !host) return null;

  const forceWebGL = new URLSearchParams(location.search).has("webgl");
  const renderer = new THREE.WebGPURenderer({ canvas, antialias: true, alpha: true, forceWebGL });
  await renderer.init();
  if (isDisposed()) {
    renderer.dispose();
    return null;
  }
  renderer.setPixelRatio(Math.min(devicePixelRatio, 1.75));
  renderer.setClearColor(0x000000, 0);
  renderer.toneMapping = THREE.NeutralToneMapping;

  const isWebGPU = Boolean(renderer.backend?.isWebGPUBackend);
  backendEl.textContent = `3D: ${isWebGPU ? "WebGPU" : "WebGL 2"}`;

  const scene = new THREE.Scene();
  const camera = new THREE.PerspectiveCamera(30, 1, 0.1, 100);
  const pmrem = new THREE.PMREMGenerator(renderer);
  const roomEnv = new RoomEnvironment();
  const envTarget = pmrem.fromScene(roomEnv, 0.04);
  scene.environment = envTarget.texture;
  roomEnv.dispose();
  pmrem.dispose();
  const key = new THREE.DirectionalLight(0xffffff, 1.6);
  key.position.set(3, 4, 5);
  scene.add(key);
  const rim = new THREE.DirectionalLight(0xffffff, 1.3);
  rim.position.set(-4, 1.5, -3);
  scene.add(rim);

  const U = {
    time: uniform(0),
    bars: uniform(0),
    slice: uniform(stage.macros.slice),
    formant: uniform(stage.macros.formant),
    open: uniform(stage.macros.open),
    chaos: uniform(stage.macros.chaos),
    twist: uniform(0),
    squash: uniform(0),
    hit: uniform(0),
    swell: uniform(0),
    seed: uniform(stage.seed),
  };
  const spectrum = Fn(([t]) => vec3(0.5).add(vec3(0.5).mul(cos(TWO_PI.mul(vec3(t).add(vec3(0.0, 0.33, 0.67)))))));
  const fresnel = Fn(() => pow(float(1).sub(abs(dot(normalView, positionViewDirection))), 3));
  const seedInt = floor(U.seed.mul(4096));

  const deform = Fn(([p]) => {
    const q = rotate(p, vec3(0, U.twist.mul(p.y), 0));
    return q.mul(vec3(float(1).sub(U.squash.mul(0.45)), float(1).add(U.squash), float(1).sub(U.squash.mul(0.45))));
  });

  const crystal = (presence, seed) => {
    const n = mx_noise_float(positionGeometry.mul(2.4).add(seed)).mul(0.5).add(0.5);
    const thr = presence.mul(1.25).sub(0.12);
    const mask = step(n, thr);
    const edge = smoothstep(thr.sub(0.12), thr, n).mul(mask).mul(step(presence, 0.995));
    return { mask, edge };
  };

  const filmThickness = (lo, hi) =>
    mix(
      float(lo),
      float(hi),
      clamp(U.formant.mul(0.65).add(mx_noise_float(TSL.positionWorld.mul(1.4)).mul(0.35).add(0.2)), 0, 1),
    );

  const glassMaterial = (opts = {}) =>
    new THREE.MeshPhysicalNodeMaterial({
      color: 0xe8e6f4,
      metalness: 0.92,
      roughness: 0.12,
      clearcoat: 1,
      clearcoatRoughness: 0.05,
      iridescence: 0.9,
      iridescenceIOR: 1.6,
      ...opts,
    });
  const wireMaterial = (glow, opacity) => {
    const m = new THREE.MeshStandardNodeMaterial({
      wireframe: true,
      transparent: true,
      depthWrite: false,
      color: 0x000000,
      roughness: 1,
      metalness: 0,
    });
    m.colorNode = vec3(0);
    m.emissiveNode = glow.mul(opacity).mul(1.4);
    m.opacityNode = opacity;
    return m;
  };

  const forms = {};
  function defineForm(name, build) {
    const solid = uniform(0);
    const wire = uniform(0);
    const accent = uniform(new THREE.Color());
    const group = new THREE.Group();
    const wireOpacity = wire.mul(float(1).sub(solid.mul(0.86))).add(U.swell.mul(0.06).mul(solid));
    const update = build({ group, solid, wire, wireOpacity, accent }) ?? (() => {});
    group.visible = false;
    scene.add(group);
    forms[name] = { group, solid, wire, accent, update, s: 0, w: 0 };
  }

  registerSculptureForms({
    THREE,
    TSL,
    defineForm,
    U,
    stage,
    helpers: { spectrum, fresnel, seedInt, deform, crystal, filmThickness, glassMaterial, wireMaterial },
  });

  // Floor polar grid
  const gridColor = uniform(new THREE.Color());
  {
    const pts = [];
    for (let r = 1; r <= 4; r++) {
      for (let i = 0; i < 128; i++) {
        const a0 = (i / 128) * Math.PI * 2;
        const a1 = ((i + 1) / 128) * Math.PI * 2;
        const R = r * 0.55;
        pts.push(Math.cos(a0) * R, 0, Math.sin(a0) * R, Math.cos(a1) * R, 0, Math.sin(a1) * R);
      }
    }
    const g = new THREE.BufferGeometry();
    g.setAttribute("position", new THREE.Float32BufferAttribute(pts, 3));
    const lm = new THREE.LineBasicNodeMaterial({ transparent: true, depthWrite: false });
    lm.colorNode = gridColor;
    lm.opacityNode = float(0.22).mul(float(1).sub(TSL.length(positionGeometry.xz).div(2.4)));
    const floorLines = new THREE.LineSegments(g, lm);
    floorLines.position.y = -1.55;
    scene.add(floorLines);
  }

  // MRT bloom pipeline
  const scenePass = pass(scene, camera);
  const mrtNode = mrt({ output, emissive: vec4(emissive, output.a) });
  mrtNode.setBlendMode("emissive", new THREE.BlendMode(THREE.NormalBlending));
  scenePass.setMRT(mrtNode);
  scenePass.getTexture("emissive").type = THREE.UnsignedByteType;
  const colorTex = scenePass.getTextureNode("output");
  const glow = bloom(scenePass.getTextureNode("emissive"), 1.1, 0.55, 0);
  const pipeline = new THREE.RenderPipeline(renderer);
  pipeline.outputNode = Fn(() => {
    const g = glow;
    const a = max(colorTex.a, clamp(max(g.r, max(g.g, g.b)), 0, 1));
    return vec4(colorTex.rgb.add(g.rgb), a);
  })();

  const dark = matchMedia("(prefers-color-scheme: dark)");
  const applyTheme = () => {
    glow.strength.value = dark.matches ? 1.15 : 0.55;
    gridColor.value.set(dark.matches ? 0xc9c6e0 : 0x2b2840);
    scene.environmentIntensity = dark.matches ? 0.85 : 1.05;
    host.style.setProperty("--stage-light", dark.matches ? "24%" : "30%");
    stage.wake();
  };
  dark.addEventListener("change", applyTheme, { signal });

  let W = 1;
  let H = 1;

  // Scroll choreography with cached document-space layout rects (recomputed on resize/scroll-layout change,
  // avoiding per-frame forced synchronous reflows inside the 60fps animation loop).
  const SCULPT = 3.0;
  const shots = [
    { el: $(".hero"), anchor: [$(".hero__grab"), $(".hero__dock")], zoom: 1.32, az: 0.0, elv: 0.14 },
    { el: $("#plugins"), anchor: [$('[data-anchor="catalog"]')], zoom: 1.15, az: 0.65, elv: 0.3 },
    { el: $("#notes"), anchor: [$('[data-anchor="notes"]')], zoom: 1.35, az: 1.5, elv: 0.75 },
  ];
  let cachedLayout = [];
  let layoutDirty = true;
  const refreshLayout = () => {
    const sy = window.scrollY;
    const sx = window.scrollX;
    cachedLayout = shots.map((shot) => {
      const elRect = shot.el ? shot.el.getBoundingClientRect() : { top: 0 };
      const anchorRects = shot.anchor.map((el) => {
        if (!el) return null;
        const r = el.getBoundingClientRect();
        if (!r.width) return null;
        const isSticky = getComputedStyle(el).position === "sticky";
        return {
          el,
          isSticky,
          left: r.left + sx,
          right: r.right + sx,
          top: r.top + sy,
          bottom: r.bottom + sy,
          width: r.width,
          height: r.height,
        };
      });
      return { shot, docTop: elRect.top + sy, anchorRects };
    });
    layoutDirty = false;
  };
  const markLayoutDirty = () => {
    layoutDirty = true;
  };

  const stageRO = new ResizeObserver(([entry]) => {
    const box = entry.contentBoxSize?.[0];
    W = Math.max(1, Math.round(box ? box.inlineSize : host.clientWidth));
    H = Math.max(1, Math.round(box ? box.blockSize : host.clientHeight));
    renderer.setSize(W, H, false);
    camera.aspect = W / H;
    camera.updateProjectionMatrix();
    markLayoutDirty();
    stage.wake();
  });
  stageRO.observe(host);
  const bodyRO = new ResizeObserver(markLayoutDirty);
  bodyRO.observe(document.body);
  document.fonts?.ready?.then(markLayoutDirty);

  // Input: pointer parallax & drag-to-twist springs
  const pointer = { x: 0, y: 0, sx: 0, sy: 0 };
  addEventListener(
    "pointermove",
    (e) => {
      pointer.x = (e.clientX / innerWidth) * 2 - 1;
      pointer.y = (e.clientY / innerHeight) * 2 - 1;
      if (!stage.frozen) stage.wake();
    },
    { passive: true, signal },
  );

  const spring = { twist: 0, tv: 0, squash: 0, sv: 0, tTarget: 0, sTarget: 0, dragging: false };
  for (const zone of $$("[data-grab]")) {
    let start = null;
    zone.addEventListener(
      "pointerdown",
      (e) => {
        start = { x: e.clientX, y: e.clientY, t: performance.now(), id: e.pointerId };
        spring.dragging = true;
        stage.wake();
      },
      { signal },
    );
    zone.addEventListener(
      "pointermove",
      (e) => {
        if (!start || e.pointerId !== start.id) return;
        const dx = e.clientX - start.x;
        const dy = e.clientY - start.y;
        if (Math.abs(dx) > 6 && !zone.hasPointerCapture(e.pointerId)) zone.setPointerCapture(e.pointerId);
        spring.tTarget = Math.max(-2.6, Math.min(2.6, dx / 140));
        spring.sTarget = e.pointerType === "touch" ? 0 : Math.max(-0.35, Math.min(0.35, -dy / 600));
        stage.wake();
      },
      { signal },
    );
    const end = (e) => {
      if (!start || e.pointerId !== start.id) return;
      const moved = Math.hypot(e.clientX - start.x, e.clientY - start.y);
      if (moved < 6 && performance.now() - start.t < 400) stage.hit = 1;
      start = null;
      spring.dragging = false;
      spring.tTarget = 0;
      spring.sTarget = 0;
      stage.wake();
    };
    zone.addEventListener("pointerup", end, { signal });
    zone.addEventListener("pointercancel", end, { signal });
  }

  const cam = { dist: 8, az: 0, elv: 0.12, x: 0, y: 0 };
  const place = (entry, sx, sy) => {
    const raw = W >= 1024 ? entry.anchorRects : entry.anchorRects.slice(0, 1);
    const rects = raw
      .filter(Boolean)
      .map((r) =>
        r.isSticky
          ? r.el.getBoundingClientRect()
          : { left: r.left - sx, right: r.right - sx, top: r.top - sy, bottom: r.bottom - sy, width: r.width, height: r.height },
      )
      .filter((r) => r.width);
    if (!rects.length) return { x: W / 2, y: H / 2, size: Math.min(W, H) * 0.85 };
    const [first] = rects;
    const left = Math.min(...rects.map((r) => r.left));
    const right = Math.max(...rects.map((r) => r.right));
    const top = Math.min(...rects.map((r) => r.top));
    const bottom = Math.max(...rects.map((r) => r.bottom));
    const cy = first.top + first.height * (rects.length > 1 ? 0.62 : 0.5);
    return { x: first.left + first.width / 2, y: Math.min(H * 0.7, Math.max(H * 0.2, cy)), size: Math.min(right - left, bottom - top) };
  };

  let scrollDirty = true;
  let cachedShotTarget = null;
  const shotTarget = () => {
    if (layoutDirty) refreshLayout();
    if (!scrollDirty && cachedShotTarget) return cachedShotTarget;
    const sy = window.scrollY;
    const sx = window.scrollX;
    const mid = innerHeight * 0.5;
    let i = 0;
    for (let k = 0; k < cachedLayout.length; k++) {
      if (cachedLayout[k].docTop - sy <= mid) i = k;
    }
    const ea = cachedLayout[i];
    const eb = cachedLayout[Math.min(i + 1, cachedLayout.length - 1)];
    const topA = ea.docTop - sy;
    const topB = eb.docTop - sy;
    const t =
      ea === eb
        ? 0
        : THREE.MathUtils.smootherstep(Math.min(1, Math.max(0, (mid - topA) / Math.max(1, topB - topA))), 0, 1);
    const pa = place(ea, sx, sy);
    const pb = place(eb, sx, sy);
    const lerp = THREE.MathUtils.lerp;
    const size = Math.max(140, lerp(pa.size, pb.size, t));
    const dist =
      ((SCULPT * H) / (size * 2 * Math.tan(THREE.MathUtils.degToRad(camera.fov / 2)))) *
      lerp(ea.shot.zoom, eb.shot.zoom, t);
    cachedShotTarget = {
      dist,
      az: lerp(ea.shot.az, eb.shot.az, t),
      elv: lerp(ea.shot.elv, eb.shot.elv, t),
      x: lerp(pa.x, pb.x, t),
      y: lerp(pa.y, pb.y, t),
    };
    scrollDirty = false;
    return cachedShotTarget;
  };
  addEventListener(
    "scroll",
    () => {
      scrollDirty = true;
      stage.wake();
    },
    { passive: true, signal },
  );

  const tmpColor = new THREE.Color();
  function oklchToLinear(L, C, hDeg, out) {
    const h = (hDeg * Math.PI) / 180;
    const a = C * Math.cos(h);
    const b = C * Math.sin(h);
    const l = (L + 0.3963377774 * a + 0.2158037573 * b) ** 3;
    const m = (L - 0.1055613458 * a - 0.0638541728 * b) ** 3;
    const s = (L - 0.0894841775 * a - 1.291485548 * b) ** 3;
    const clip = (v) => Math.min(1, Math.max(0, v));
    return out.setRGB(
      clip(4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s),
      clip(-1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s),
      clip(-0.0041960863 * l - 0.7034186147 * m + 1.707614701 * s),
      THREE.LinearSRGBColorSpace,
    );
  }

  let running = false;
  let visibleStage = true;
  let last = performance.now();
  let simTime = 0;
  let idleAz = 0;
  let awakeUntil = 0;
  let firstFrame = true;
  const lightPos = { x: "", y: "" };
  const damp = (cur, target, rate, dt) => cur + (target - cur) * (1 - Math.exp(-rate * dt));

  function frame() {
    const now = performance.now();
    const dt = Math.min(0.05, (now - last) / 1000);
    last = now;
    const moving = !stage.frozen;
    if (moving) simTime += dt;

    for (const k of ["slice", "formant", "open", "chaos"]) U[k].value = damp(U[k].value, stage.macros[k], 10, dt);
    U.seed.value = stage.seed;
    U.time.value = simTime;
    if (moving) U.bars.value += (dt * (stage.bpm || 120)) / 240;
    U.swell.value = moving ? 0.5 - 0.5 * Math.cos(Math.PI * U.bars.value) : 0;
    stage.hit = Math.max(0, stage.hit - dt * 3.2);
    U.hit.value = Math.pow(stage.hit, 2);

    spring.tv += ((spring.tTarget - spring.twist) * 38 - spring.tv * 8.5) * dt;
    spring.twist += spring.tv * dt;
    spring.sv += ((spring.sTarget - spring.squash) * 46 - spring.sv * 9) * dt;
    spring.squash += spring.sv * dt;
    U.twist.value = spring.twist;
    U.squash.value = spring.squash;

    let settled = Math.abs(spring.tv) < 1e-3 && Math.abs(spring.twist - spring.tTarget) < 1e-3 && stage.hit === 0;
    for (const [name, P] of Object.entries(forms)) {
      const on = name === stage.form;
      if (reducedMotion.matches || firstFrame) {
        P.w = on ? 1 : 0;
        P.s = on ? 1 : 0;
      } else {
        P.w = damp(P.w, on ? 1 : 0, on ? 7 : 5, dt);
        P.s = damp(P.s, on && P.w > 0.55 ? 1 : 0, on ? 2.4 : 5, dt);
      }
      P.wire.value = P.w;
      P.solid.value = P.s;
      P.group.visible = P.w > 0.003 || P.s > 0.003;
      P.group.scale.setScalar(0.9 + 0.1 * P.s);
      if (P.group.visible) P.update(dt, moving);
      settled &&= Math.abs(P.w - (on ? 1 : 0)) < 0.002 && Math.abs(P.s - (on ? 1 : 0)) < 0.002;
    }
    const accentColor = oklchToLinear(0.78, 0.16, stage.hue, tmpColor);
    for (const P of Object.values(forms)) P.accent.value.lerp(accentColor, 1 - Math.exp(-6 * dt));
    rim.color.lerp(accentColor, 1 - Math.exp(-4 * dt));

    const target = shotTarget();
    const motionOK = moving && !reducedMotion.matches;
    pointer.sx = damp(pointer.sx, motionOK ? pointer.x : 0, 4, dt);
    pointer.sy = damp(pointer.sy, motionOK ? pointer.y : 0, 4, dt);
    if (motionOK) idleAz += dt * 0.05;
    const r = reducedMotion.matches || firstFrame ? 1 : 1 - Math.exp(-5 * dt);
    cam.dist += (target.dist - cam.dist) * r;
    cam.az += (target.az - cam.az) * r;
    cam.elv += (target.elv - cam.elv) * r;
    cam.x += (target.x - cam.x) * r;
    cam.y += (target.y - cam.y) * r;
    const az = cam.az + idleAz + pointer.sx * 0.18;
    const elv = cam.elv + pointer.sy * 0.08;
    camera.position.set(
      Math.sin(az) * Math.cos(elv) * cam.dist,
      Math.sin(elv) * cam.dist,
      Math.cos(az) * Math.cos(elv) * cam.dist,
    );
    camera.lookAt(0, 0, 0);
    camera.setViewOffset(W, H, W / 2 - cam.x, H / 2 - cam.y, W, H);

    const lx = `${((cam.x / W) * 100).toFixed(1)}%`;
    const ly = `${((cam.y / H) * 100).toFixed(1)}%`;
    if (lx !== lightPos.x) host.style.setProperty("--stage-x", (lightPos.x = lx));
    if (ly !== lightPos.y) host.style.setProperty("--stage-y", (lightPos.y = ly));
    settled &&= Math.abs(target.x - cam.x) < 0.5 && Math.abs(target.dist - cam.dist) < 0.01;

    pipeline.render();
    if (firstFrame) {
      firstFrame = false;
      root.dataset.stage = "live";
    }

    if (!moving && settled && now > awakeUntil) setRunning(false);
  }

  function setRunning(on) {
    on = on && visibleStage && !document.hidden;
    if (on === running) return;
    running = on;
    if (on) last = performance.now();
    renderer.setAnimationLoop(on ? frame : null);
    if (!on) renderer._animation?.stop?.();
  }
  stage.wake = () => {
    awakeUntil = performance.now() + 400;
    setRunning(true);
  };

  let cvRelevant = true;
  let onScreen = true;
  const updateVisibility = () => {
    visibleStage = cvRelevant && onScreen;
    visibleStage ? stage.wake() : setRunning(false);
  };
  if ("contentVisibility" in root.style) {
    host.addEventListener("contentvisibilityautostatechange", (e) => { cvRelevant = !e.skipped; updateVisibility(); }, { signal });
  }
  const stageIO = new IntersectionObserver(([e]) => { onScreen = e.isIntersecting; updateVisibility(); });
  stageIO.observe(host);
  document.addEventListener("visibilitychange", () => (document.hidden ? setRunning(false) : stage.wake()), { signal });

  canvas.addEventListener(
    "webglcontextlost",
    (e) => {
      e.preventDefault();
      setRunning(false);
      root.dataset.stage = "fallback";
      backendEl.textContent = "3D paused · context lost";
    },
    { signal },
  );

  const cleanup = () => {
    setRunning(false);
    stageRO.disconnect();
    bodyRO.disconnect();
    stageIO.disconnect();
    scene.traverse((obj) => {
      obj.geometry?.dispose?.();
      if (Array.isArray(obj.material)) obj.material.forEach((m) => m?.dispose?.());
      else obj.material?.dispose?.();
    });
    glow?.dispose?.();
    scenePass?.dispose?.();
    pipeline?.dispose?.();
    envTarget.dispose();
    renderer.dispose();
  };

  try {
    for (const P of Object.values(forms)) P.group.visible = true;
    await renderer.compileAsync(scene, camera);
    if (isDisposed()) {
      cleanup();
      return null;
    }
    applyTheme();
    stage.wake();
    return cleanup;
  } catch (err) {
    cleanup();
    throw err;
  }
}
