// Hoberman linkage sphere cursor — decorative canvas trailing mouse/pen pointers, sleeping when idle.

export function startHoberman(reducedMotion, signal) {
  const fine = matchMedia("(hover: hover) and (pointer: fine)");
  const forced = matchMedia("(forced-colors: active)");
  let toy = null;
  const sync = () => {
    const want = fine.matches && !reducedMotion.matches && !forced.matches;
    if (want && !toy) toy = createHoberman();
    else if (!want && toy) {
      toy.destroy();
      toy = null;
    }
  };
  [fine, forced, reducedMotion].forEach((mq) => mq.addEventListener("change", sync, { signal }));
  sync();
  return () => {
    toy?.destroy();
    toy = null;
  };
}

function createHoberman() {
  const SIZE = 180;
  const HALF = SIZE / 2;
  const N = 10;
  const canvas = Object.assign(document.createElement("canvas"), { className: "hoberman" });
  canvas.setAttribute("aria-hidden", "true");
  const dpr = Math.min(2, devicePixelRatio || 1);
  canvas.width = canvas.height = Math.round(SIZE * dpr);
  document.body.append(canvas);
  const ctx = canvas.getContext("2d");
  ctx.scale(dpr, dpr);
  ctx.lineCap = "round";

  const PHI = (1 + Math.sqrt(5)) / 2;
  const norm = (v) => {
    const l = Math.hypot(...v);
    return v.map((c) => c / l);
  };
  const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
  const rings = [
    [0, 1, PHI],
    [0, 1, -PHI],
    [1, PHI, 0],
    [1, -PHI, 0],
    [PHI, 0, 1],
    [-PHI, 0, 1],
  ].map((axis) => {
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
  const dark = matchMedia("(prefers-color-scheme: dark)");
  const s = {
    x: innerWidth / 2,
    y: innerHeight / 2,
    vx: 0,
    vy: 0,
    px: innerWidth / 2,
    py: innerHeight / 2,
    R: 10,
    rv: 0,
    open: 0.1,
    ov: 0,
    ax: 0.5,
    ay: 0,
    az: 0,
    hue: 285,
    alpha: 0,
    squeeze: 1,
    target: null,
    mode: "free",
    seen: false,
    lastMove: 0,
  };
  let raf = 0;
  let last = performance.now();

  const hueOf = (el) => {
    const scope = el?.closest?.('[style*="--hue"]');
    const h = scope && parseFloat(getComputedStyle(scope).getPropertyValue("--hue"));
    return Number.isFinite(h) ? h : 285;
  };
  const onMove = (e) => {
    if (e.pointerType === "touch") return;
    if (s.seen && e.clientX === s.px && e.clientY === s.py) return;
    s.px = e.clientX;
    s.py = e.clientY;
    s.lastMove = performance.now();
    if (!s.seen) {
      s.x = s.px;
      s.y = s.py;
      s.seen = true;
    }
    wake();
  };
  const onOver = (e) => {
    const el = e.target;
    if (el.closest?.(HIDE_OVER)) {
      s.mode = "hidden";
      s.target = null;
    } else if (el.closest?.("[data-grab]")) {
      s.mode = "grab";
      s.target = null;
    } else {
      const hit = el.closest?.(CLICKABLE);
      s.mode = hit ? "over" : "free";
      s.target = hit || null;
    }
    s.hueTarget = hueOf(s.target || el);
    wake();
  };
  const onDown = () => { s.squeeze = 0.55; wake(); };
  const onUp = () => { s.squeeze = 1; wake(); };
  const onLeave = () => { s.mode = "hidden"; wake(); };
  addEventListener("pointermove", onMove, { passive: true });
  addEventListener("pointerover", onOver, { passive: true });
  addEventListener("pointerdown", onDown, { passive: true });
  addEventListener("pointerup", onUp, { passive: true });
  document.documentElement.addEventListener("pointerleave", onLeave);
  addEventListener("blur", onLeave);

  function wake() {
    if (!raf && !document.hidden) {
      last = performance.now();
      raf = requestAnimationFrame(tick);
    }
  }

  function tick(now) {
    raf = 0;
    const dt = Math.min(0.04, (now - last) / 1000);
    last = now;

    let tx = s.px;
    let ty = s.py;
    let RT = 10;
    let openT = 0.12;
    if (s.mode === "over" && s.target?.isConnected) {
      const r = s.target.getBoundingClientRect();
      if (r.width < 340 && r.height < 140) {
        tx += (r.left + r.width / 2 - tx) * 0.28;
        ty += (r.top + r.height / 2 - ty) * 0.28;
      }
      RT = Math.min(48, Math.max(22, Math.min(r.width, r.height) * 0.62 + 6));
      openT = 1;
    } else if (s.mode === "grab") {
      RT = 56;
      openT = 0.85;
    }
    const alphaT = s.seen && s.mode !== "hidden" ? (s.mode === "over" ? 0.7 : 1) : 0;

    s.vx += ((tx - s.x) * 210 - s.vx * 24) * dt;
    s.x += s.vx * dt;
    s.vy += ((ty - s.y) * 210 - s.vy * 24) * dt;
    s.y += s.vy * dt;
    s.rv += ((RT * s.squeeze - s.R) * 170 - s.rv * 15) * dt;
    s.R += s.rv * dt;
    s.ov += ((openT * s.squeeze - s.open) * 120 - s.ov * 13) * dt;
    s.open += s.ov * dt;
    s.alpha += (alphaT - s.alpha) * (1 - Math.exp(-10 * dt));
    s.hue += ((((s.hueTarget ?? 285) - s.hue + 540) % 360) - 180) * (1 - Math.exp(-6 * dt));
    const speed = Math.hypot(s.vx, s.vy);
    s.ay += dt * (0.35 + Math.min(speed, 2500) * 0.0012 + (s.mode === "grab" ? 0.8 : 0));
    s.ax += (0.45 + s.vy * 0.0008 - s.ax) * (1 - Math.exp(-4 * dt));
    s.az += (-s.vx * 0.0006 - s.az) * (1 - Math.exp(-4 * dt));

    draw();
    canvas.style.transform = `translate3d(${(s.x - HALF).toFixed(1)}px, ${(s.y - HALF).toFixed(1)}px, 0)`;

    const settled =
      speed < 2 && Math.abs(s.rv) < 0.05 && Math.abs(s.ov) < 0.01 && Math.abs(s.alpha - alphaT) < 0.01;
    if (!(settled && now - s.lastMove > 1400)) raf = requestAnimationFrame(tick);
  }

  function draw() {
    ctx.clearRect(0, 0, SIZE, SIZE);
    if (s.alpha < 0.01) return;
    const open = Math.min(1.2, Math.max(0, s.open));
    const R = Math.max(4, s.R);
    const d = R * (0.5 - 0.42 * Math.min(1, open));
    const [cx, sx] = [Math.cos(s.ax), Math.sin(s.ax)];
    const [cy, sy] = [Math.cos(s.ay), Math.sin(s.ay)];
    const [cz, sz] = [Math.cos(s.az), Math.sin(s.az)];
    const rot = ([x, y, z]) => {
      const X = x * cy + z * sy;
      let Z = -x * sy + z * cy;
      const Y = y * cx - Z * sx;
      Z = y * sx + Z * cx;
      return [X * cz - Y * sz, X * sz + Y * cz, Z];
    };
    const F = 260;
    const proj = (v, r) => {
      const [x, y, z] = rot(v);
      const k = F / (F - z * r);
      return [HALF + x * r * k, HALF + y * r * k, z];
    };
    const L = dark.matches ? 0.82 : 0.55;
    ctx.lineWidth = 1.15;
    rings.forEach((ring, k) => {
      const hue = (s.hue + k * 24) % 360;
      for (let i = 0; i < N; i++) {
        const a = ring[i];
        const b = ring[(i + 1) % N];
        const p1 = proj(a, R + d);
        const p2 = proj(b, R - d);
        const p3 = proj(a, R - d);
        const p4 = proj(b, R + d);
        const depth = (p1[2] + p2[2] + 1) / 2;
        ctx.strokeStyle = `oklch(${L} 0.15 ${hue.toFixed(0)} / ${(s.alpha * (0.18 + 0.82 * depth)).toFixed(3)})`;
        ctx.beginPath();
        ctx.moveTo(p1[0], p1[1]);
        ctx.lineTo(p2[0], p2[1]);
        ctx.moveTo(p3[0], p3[1]);
        ctx.lineTo(p4[0], p4[1]);
        ctx.stroke();
        if (depth > 0.55) {
          ctx.fillStyle = ctx.strokeStyle;
          ctx.fillRect(p1[0] - 1, p1[1] - 1, 2, 2);
        }
      }
    });
  }

  return {
    destroy() {
      cancelAnimationFrame(raf);
      removeEventListener("pointermove", onMove);
      removeEventListener("pointerover", onOver);
      removeEventListener("pointerdown", onDown);
      removeEventListener("pointerup", onUp);
      document.documentElement.removeEventListener("pointerleave", onLeave);
      removeEventListener("blur", onLeave);
      canvas.remove();
    },
  };
}
