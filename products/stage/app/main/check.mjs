// The checks a scene or a Cut must pass before it is kept. They run in offscreen windows, which
// paint without a display at a rate we choose, so a check takes seconds and the visible Stage
// keeps its normal frame rate. A scene is stepped through three bars of a drum pattern it has
// never seen, and the pictures are compared.
import { BrowserWindow } from "electron";
import { createHash } from "node:crypto";

const BANNED = [
  ["Date", /\bDate\s*[.(]|new\s+Date\b/], ["performance.now", /performance\s*\.\s*now/], ["requestAnimationFrame", /requestAnimationFrame/],
  ["setTimeout", /setTimeout/], ["setInterval", /setInterval/], ["Math.random", /Math\s*\.\s*random/],
  ["transition", /(^|[\s;{"'])transition(-[a-z]+)?\s*:/m], ["external URL", /(src|href)\s*=\s*["']?https?:|url\(\s*["']?https?:|@import|\bfetch\s*\(|import\s+.*from/],
];
const STEPS = 8;                                           // frames per beat
const BAR = STEPS * 4;
const MOVED = { kicks: [0.5, 2], snares: [1.5, 3.5] };     // shares no position with the usual pattern
const USUAL = [0, 1, 2.5, 3], PLAYED = [0.5, 1.5, 2, 3.5];
const mean = (values) => values.reduce((sum, value) => sum + value, 0) / (values.length || 1);
const difference = (a, b) => { let sum = 0; for (let i = 0; i < a.length; i += 1) sum += Math.abs(a[i] - b[i]); return sum / a.length; };

// A small grey copy of a captured frame, for comparing pictures.
function small(image) {
  const pixels = image.resize({ width: 160, height: 90 }).toBitmap();
  const grey = new Uint8Array(pixels.length / 4);
  for (let i = 0; i < grey.length; i += 1) grey[i] = (pixels[i * 4] + pixels[i * 4 + 1] + pixels[i * 4 + 2]) / 3;
  return grey;
}

const LIMIT_MS = 45000;

// A scene that loops forever, or a window that stops drawing, must not hang the agent: a check that
// runs past the limit is abandoned, its window thrown away, and the agent told the scene froze.
export const createChecker = (origin) => limited(() => openChecker(origin));
// The same for a Cut, played between the two scenes of `setUrl` (a set whose clip overlaps them).
export const createCutChecker = (origin, setUrl) => limited(() => openCutChecker(origin, setUrl));

// A checker opens its window on first use and keeps it for the job. `dispose` closes it; a hidden
// window left open would keep the app alive after its own window had closed.
function limited(open) {
  let checking = null;
  check.dispose = () => { checking?.window.destroy(); checking = null; };
  return check;
  async function check(url, source) {
    checking ??= await open();
    let timer;
    const frozen = new Promise((resolve) => { timer = setTimeout(() => resolve(null), LIMIT_MS); });
    const result = await Promise.race([checking.check(url, source), frozen]);
    clearTimeout(timer);
    if (result) return result;
    checking.window.destroy();
    checking = null;
    return { report: { passed: false, errors: [`it froze: it had not finished playing after ${LIMIT_MS / 1000} seconds`] }, pictures: [] };
  }
}

// A Cut is a piece of the Stage's own shader, so it is checked in the Stage itself: load the set
// with this Cut, step across the overlap, and look at five moments of it.
const CUT_FRAMES = [126, 138, 150, 162, 174];   // progress 0.1, 0.3, 0.5, 0.7, 0.9 of a 2-beat cut at beat 4, 120 BPM, 60 fps
function openCutChecker(origin, setUrl) {
  const window = offscreenWindow();
  const page = (code) => window.webContents.executeJavaScript(code);
  const play = async (url, keep) => {
    await window.loadURL(`${origin}/index.html?w=960&h=540&fx=0&set=${encodeURIComponent(setUrl)}&cut=${encodeURIComponent(url)}`);
    await page(`new Promise((resolve) => (function poll(n) { window.report?.ready || n > 200 ? resolve() : setTimeout(() => poll(n + 1), 50); })(0))`);
    const errors = await page("window.report?.errors ?? ['the Stage did not start']");
    const hashes = [];
    for (let n = 100; n <= CUT_FRAMES.at(-1) && !errors.length; n += 1) {
      await page(`stepTo(${n}, 60, 120)`);
      if (!CUT_FRAMES.includes(n)) continue;
      hashes.push((await page("hashFrame()")).hash);
      if (keep) keep.push((await window.webContents.capturePage()).resize({ width: 640 }).toJPEG(80).toString("base64"));
    }
    return { errors: [...new Set(await page("window.report?.errors ?? []")), ...errors.filter((e) => e === "the Stage did not start")], hashes };
  };
  async function check(url, source) {
    const lint = [!/fn\s+cut\s*\(/.test(source) && "no fn cut", /@(group|binding|fragment|vertex|compute)/.test(source) && "bindings or entry points"].filter(Boolean);
    const pictures = [];
    const first = await play(url, pictures);
    const report = { passed: false, lint, errors: first.errors };
    if (first.errors.length) return { report, pictures: [] };
    report.changesAcrossTheCut = new Set(first.hashes).size === first.hashes.length;
    report.sameTwice = JSON.stringify((await play(url)).hashes) === JSON.stringify(first.hashes);
    report.passed = !lint.length && report.changesAcrossTheCut && report.sameTwice;
    return { report, pictures };
  }
  return { window, check };
}

function offscreenWindow() {
  const window = new BrowserWindow({ width: 960, height: 540, useContentSize: true, show: false, webPreferences: { offscreen: true, backgroundThrottling: false } });
  window.webContents.setFrameRate(240);
  return window;
}

async function openChecker(origin) {
  const window = offscreenWindow();
  await window.loadURL(`${origin}/score.html`);
  const page = (code) => window.webContents.executeJavaScript(code);
  const step = (n) => page(`step(${n}, ${(174 / 60) * STEPS}, 174, ${JSON.stringify(MOVED)})`);

  // Returns { report, pictures }: the report is what the agent reads, the pictures what it sees.
  return { window, check };
  async function check(url, source) {
    const report = { passed: false, lint: BANNED.filter(([, pattern]) => pattern.test(source)).map(([name]) => name) };
    try {
      report.elements = (await page(`load(${JSON.stringify(url)})`)).elements;
    } catch (error) {
      return { report: { ...report, errors: [`the scene did not load: ${error.message}`] }, pictures: [] };
    }
    const frames = [], hashes = [], pictures = [], times = [];
    for (let n = 0; n < BAR * 3; n += 1) {
      times.push(await step(n));
      const image = await window.webContents.capturePage();
      frames.push(small(image));
      hashes.push(createHash("sha1").update(image.toBitmap()).digest("hex"));
      // Bar 2: just after a kick, just after a snare, a quiet moment, and the end of the bar.
      if ([BAR + 5, BAR + 13, BAR + 23, BAR * 2 - 1].includes(n)) pictures.push(image.resize({ width: 640 }).toJPEG(80).toString("base64"));
    }
    report.errors = await page("errors()");
    report.blank = Math.max(...frames.map((frame) => mean([...frame]))) < 2;
    report.scriptMsPerFrame = Number(mean(times).toFixed(2));

    // Does the picture move when the drums hit, wherever they hit?
    const motion = frames.slice(1).map((frame, i) => difference(frame, frames[i]));
    const position = (i) => ((i + 1) % BAR) / STEPS;
    const at = (hits) => mean(motion.filter((_, i) => hits.includes(position(i))));
    const elsewhere = mean(motion.filter((_, i) => ![...USUAL, ...PLAYED].includes(position(i)))) || 1e-6;
    const onHits = Number((at(PLAYED) / elsewhere).toFixed(1)), onUsual = Number((at(USUAL) / elsewhere).toFixed(1));
    report.followsDrums = onHits > 2 && onUsual < 2;
    report.motion = { onTheHitsPlayed: onHits, whereHitsUsuallyAre: onUsual, note: "times the motion between hits; needs more than 2 on the hits played and less than 2 where they usually are" };

    // The same again from a fresh load: every frame must be identical.
    await page(`load(${JSON.stringify(url)})`);
    let same = 0;
    for (let n = 0; n < BAR; n += 1) {
      await step(n);
      if (createHash("sha1").update((await window.webContents.capturePage()).toBitmap()).digest("hex") === hashes[n]) same += 1;
    }
    report.sameTwice = same === BAR;
    report.passed = !report.lint.length && !report.errors.length && !report.blank && report.followsDrums && report.sameTwice;
    return { report, pictures };
  }
}
