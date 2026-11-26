// The scene host: everything the draft skill promises a scene, in about forty lines.
// Used unchanged by the compositor (stage.js) and by the scoring page (score.html).
const HOOKS = ["beat", "phase", "bar", "bpm", "t", "age", "level", "low", "mid", "high", "onset", "kick", "snare", "hat", "progress"];

const register = (win, name, initialValue) => {
  try {
    win.CSS.registerProperty({ name: `--${name}`, syntax: "<number>", inherits: true, initialValue: String(initialValue) });
  } catch {
    // The scene registered it itself with @property. Its definition stands.
  }
};

export async function loadScene(iframe, url) {
  await new Promise((resolve, reject) => {
    iframe.addEventListener("load", resolve, { once: true });
    iframe.addEventListener("error", reject, { once: true });
    iframe.src = url;
  });
  const win = iframe.contentWindow;
  const doc = win.document;
  const errors = [];
  win.addEventListener("error", (e) => errors.push(String(e.message)));
  win.addEventListener("unhandledrejection", (e) => errors.push(String(e.reason)));

  const manifest = JSON.parse(doc.getElementById("scene")?.textContent ?? "{}");
  const params = Object.fromEntries((manifest.params ?? []).map((p) => [p.name, p.default]));
  HOOKS.forEach((name) => register(win, name, 0));
  Object.entries(params).forEach(([name, value]) => register(win, name, value));

  await doc.fonts.ready;
  if (win.ready) await win.ready;
  return { iframe, win, doc, manifest, params, errors, url };
}

// One frame: properties, then the event, then the animation clock (1 s of animation = 1 beat).
export function drive(scene, frame) {
  const style = scene.doc.documentElement.style;
  for (const name of HOOKS) style.setProperty(`--${name}`, frame[name] ?? 0);
  for (const [name, value] of Object.entries(scene.params)) style.setProperty(`--${name}`, value);
  try {
    scene.win.dispatchEvent(new CustomEvent("frame", { detail: { ...frame, params: scene.params } }));
  } catch (error) {
    scene.errors.push(String(error));
  }
  for (const animation of scene.doc.getAnimations()) {
    if (animation.playState !== "paused") animation.pause();
    animation.currentTime = frame.beat * 1000;
  }
}
