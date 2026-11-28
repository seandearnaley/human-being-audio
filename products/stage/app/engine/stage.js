// The Stage's output page. It runs in its own frame, on its own origin, and holds nothing worth
// stealing: scenes are files other people or a model wrote, and they live here, not in the app.
//   ?set=<url>        the set to play: layers of note -> scene, a Cut and a Look
//   ?layers=<url,...> or plain scenes stacked, for a preview
//   ?live=port        lines from the plugins arrive on a MessagePort the app hands over
//   ?take=<url>       a recorded take, replayed on the stepped clock (export)
//
// The compositor. Each scene is a same-origin iframe inside <canvas content="drawable">.
// On every paint, HTML-in-Canvas draws each scene's snapshot into its own WebGPU texture,
// one WGSL pass layers and filters them, and the result goes to the screen.
import { loadScene, drive } from "./host.js";
import { frameAt, frameAtTime } from "./take.js";
import { connectHub, deliver, alive, nowMicros, frameFromLink, applyMacros } from "./link.js";
import { layerAt } from "./arrangement.js";

const query = new URLSearchParams(location.search);
const W = Number(query.get("w") ?? 1920);
const H = Number(query.get("h") ?? 1080);
const LAYOUT_W = 1920;
const LAYOUT_H = 1080;
const MAX_LAYERS = 4;
const ARRANGED_LAYERS = 2;   // each needs two textures, outgoing and incoming, and the pass binds four
// With ?set=, layers are arranged by notes: each layer of the set maps notes to scenes (see
// arrangement.js). A set with no `layers` is one layer.
const set = query.get("set") ? await (await fetch(query.get("set"))).json() : null;
const setLayers = set ? (set.layers ?? [set]).slice(0, ARRANGED_LAYERS) : [];
const urls = set ? setLayers.flatMap((layer) => Object.values(layer.scenes)) : (query.get("layers") ?? "").split(",").filter(Boolean).slice(0, MAX_LAYERS);
const useLook = query.get("fx") !== "0";
// With ?take=, a recorded take: a header line, then every line the plugins sent while it ran.
const take = query.get("take") ? await (async (url) => {
  const [header, ...lines] = (await (await fetch(url)).text()).trim().split("\n").map((line) => JSON.parse(line));
  return { url, header, lines, cursor: 0, links: new Map() };
})(query.get("take")) : null;

const canvas = document.getElementById("out");
canvas.width = W;
canvas.height = H;

const report = { api: {}, errors: [], layers: [] };
window.report = report;
report.api = {
  contentAttribute: canvas.content,
  requestPaint: typeof canvas.requestPaint,
  drawElementImageToTexture: typeof GPUQueue !== "undefined" && "drawElementImageToTexture" in GPUQueue.prototype,
  drawElementImage: "drawElementImage" in CanvasRenderingContext2D.prototype,
};

const adapter = await navigator.gpu.requestAdapter();
const device = await adapter.requestDevice();
device.addEventListener("uncapturederror", (e) => report.errors.push(String(e.error.message)));
const context = canvas.getContext("webgpu");
const presentFormat = navigator.gpu.getPreferredCanvasFormat();
context.configure({ device, format: presentFormat, alphaMode: "opaque" });

// ---- scenes ----
const scenes = [];
for (const url of urls) {
  const iframe = document.createElement("iframe");
  iframe.setAttribute("drawable", "");
  iframe.style.cssText = `width:${LAYOUT_W}px;height:${LAYOUT_H}px;border:0;display:block;background:transparent`;
  canvas.append(iframe);
  scenes.push(await loadScene(iframe, url));
}

// ---- GPU resources ----
const texture = (usage, width = W, height = H) =>
  device.createTexture({ size: [width, height], format: "rgba8unorm", usage });
const LAYER_USAGE = GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST | GPUTextureUsage.RENDER_ATTACHMENT;
const TARGET_USAGE = GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC;
// Arranged: two textures a layer, the outgoing and the incoming scene. Otherwise one per layer.
const layerTextures = (set ? setLayers.flatMap(() => [0, 1]) : scenes).map(() => texture(LAYER_USAGE));
// Each arranged layer: its own note map, Cut length, notes heard and current state.
let loaded = 0;
const arrangedLayers = setLayers.map((layer) => ({
  cutBeats: layer.cutBeats,
  sceneForNote: new Map(Object.keys(layer.scenes).map((note) => [Number(note), scenes[loaded++]])),
  notes: (layer.clip ?? []).map(([note, on, off]) => ({ note, on, off })),
  state: null,
}));
const empty = texture(LAYER_USAGE, 1, 1);
const targets = [texture(TARGET_USAGE), texture(TARGET_USAGE)];
const sampler = device.createSampler({ magFilter: "linear", minFilter: "linear" });
const uniforms = device.createBuffer({ size: 80, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });

const lookSource = await (await fetch(set?.look ?? "look.wgsl")).text();
const cutSource = await (await fetch(query.get("cut") ?? set?.cut ?? "cut.wgsl")).text();   // ?cut= plays another Cut
const shader = device.createShaderModule({
  code: /* wgsl */ `
struct Uniforms {
  res: vec2f, beat: f32, phase: f32,
  low: f32, mid: f32, high: f32, onset: f32,
  opacity: vec4f,
  count: f32, fx: f32, bar: f32, n: f32,
  progress: vec2f, arranged: f32, spare: f32,
};
@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var samp: sampler;
@group(0) @binding(2) var layer0: texture_2d<f32>;
@group(0) @binding(3) var layer1: texture_2d<f32>;
@group(0) @binding(4) var layer2: texture_2d<f32>;
@group(0) @binding(5) var layer3: texture_2d<f32>;
@group(0) @binding(6) var prev: texture_2d<f32>;

@vertex fn vertex(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
  let corners = array(vec2f(-1, -1), vec2f(3, -1), vec2f(-1, 3));
  return vec4f(corners[i], 0, 1);
}

// Premultiplied "over", bottom layer first.
fn over(below: vec4f, above: vec4f, opacity: f32) -> vec4f {
  let a = above * opacity;
  return a + below * (1.0 - a.a);
}
// Arranged: each layer has an outgoing and an incoming scene, and the Cut joins them. A Cut only
// ever calls scene_from and scene_to, so arranging points those at the layer being drawn.
var<private> arranging: u32 = 0;
fn scene_from(uv: vec2f) -> vec4f {
  if (arranging == 0) { return textureSampleLevel(layer0, samp, uv, 0.0) * u.opacity.x; }
  return textureSampleLevel(layer2, samp, uv, 0.0) * u.opacity.z;
}
fn scene_to(uv: vec2f) -> vec4f {
  if (arranging == 0) { return textureSampleLevel(layer1, samp, uv, 0.0) * u.opacity.y; }
  return textureSampleLevel(layer3, samp, uv, 0.0) * u.opacity.w;
}
${cutSource}
fn arranged_layer(uv: vec2f, progress: f32, has_from: f32) -> vec4f {
  if (progress >= 1.0 || has_from < 0.5) { return scene_to(uv); }
  return cut(uv, progress);
}
fn layers(uv: vec2f) -> vec4f {
  if (u.arranged > 0.5) {
    arranging = 0;
    let below = arranged_layer(uv, u.progress.x, u.opacity.x);
    arranging = 1;
    return over(below, arranged_layer(uv, u.progress.y, u.opacity.z), 1.0);
  }
  var colour = vec4f(0.0);
  colour = over(colour, textureSampleLevel(layer0, samp, uv, 0.0), u.opacity.x);
  colour = over(colour, textureSampleLevel(layer1, samp, uv, 0.0), u.opacity.y);
  colour = over(colour, textureSampleLevel(layer2, samp, uv, 0.0), u.opacity.z);
  colour = over(colour, textureSampleLevel(layer3, samp, uv, 0.0), u.opacity.w);
  return colour;
}

${lookSource}

@fragment fn composite(@builtin(position) position: vec4f) -> @location(0) vec4f {
  let uv = position.xy / u.res;
  if (u.fx < 0.5) { return vec4f(layers(uv).rgb, 1.0); }
  return look(uv);
}
@fragment fn present(@builtin(position) position: vec4f) -> @location(0) vec4f {
  return textureSampleLevel(prev, samp, position.xy / u.res, 0.0);
}`,
});
const shaderInfo = await shader.getCompilationInfo();
shaderInfo.messages.filter((m) => m.type === "error").forEach((m) => report.errors.push(`wgsl:${m.lineNum} ${m.message}`));

const layout = device.createBindGroupLayout({
  entries: [
    { binding: 0, visibility: GPUShaderStage.FRAGMENT, buffer: {} },
    { binding: 1, visibility: GPUShaderStage.FRAGMENT, sampler: {} },
    ...[2, 3, 4, 5, 6].map((binding) => ({ binding, visibility: GPUShaderStage.FRAGMENT, texture: {} })),
  ],
});
const pipelineLayout = device.createPipelineLayout({ bindGroupLayouts: [layout] });
const pipeline = (entryPoint, format) =>
  device.createRenderPipeline({
    layout: pipelineLayout,
    vertex: { module: shader, entryPoint: "vertex" },
    fragment: { module: shader, entryPoint, targets: [{ format }] },
  });
const compositePipeline = pipeline("composite", "rgba8unorm");
const presentPipeline = pipeline("present", presentFormat);

// bindings[i] reads targets[i] as "prev". Compositing writes the other target, then presenting reads it.
const bindings = targets.map((target) =>
  device.createBindGroup({
    layout,
    entries: [
      { binding: 0, resource: { buffer: uniforms } },
      { binding: 1, resource: sampler },
      ...[0, 1, 2, 3].map((i) => ({ binding: 2 + i, resource: (layerTextures[i] ?? empty).createView() })),
      { binding: 6, resource: target.createView() },
    ],
  }),
);

// ---- one frame ----
let frame = frameAt(0, { width: W, height: H });
let flip = 0;
let afterPaint = null;
let captureFailed = false;
let capture = null;

function pass(encoder, view, renderPipeline, bindGroup) {
  const renderPass = encoder.beginRenderPass({
    colorAttachments: [{ view, loadOp: "clear", storeOp: "store", clearValue: [0, 0, 0, 1] }],
  });
  renderPass.setPipeline(renderPipeline);
  renderPass.setBindGroup(0, bindGroup);
  renderPass.draw(3);
  renderPass.end();
}

// Chrome fires paint whenever a scene's picture changes, not only on requestPaint().
// The feedback trail makes every composite stateful, so composite exactly once per requested frame.
let requested = false;
canvas.onpaint = () => {
  if (!requested) return;
  requested = false;
  const t0 = performance.now();
  const sources = set ? arrangedLayers.flatMap((layer) => [layer.state?.from, layer.state?.to].map((n) => n && layer.sceneForNote.get(n.note))) : scenes;
  sources.forEach((scene, i) => {
    if (!scene) return;
    try {
      device.queue.drawElementImageToTexture(
        { source: scene.iframe },
        { texture: layerTextures[i], size: [W, H], premultipliedAlpha: true },
      );
    } catch (error) {
      if (!captureFailed) report.errors.push(`capture ${scene.url}: ${error}`);
      captureFailed = true;
    }
  });
  const t1 = performance.now();

  const opacity = [0, 1, 2, 3].map((i) => (sources[i] ? 1 : 0));
  device.queue.writeBuffer(uniforms, 0, new Float32Array([
    W, H, frame.beat, frame.phase, frame.low, frame.mid, frame.high, frame.onset,
    ...opacity, scenes.length, useLook ? 1 : 0, frame.bar, frame.n,
    arrangedLayers[0]?.state?.progress ?? 1, arrangedLayers[1]?.state?.progress ?? 1, set ? 1 : 0, 0,
  ]));
  const encoder = device.createCommandEncoder();
  pass(encoder, targets[1 - flip].createView(), compositePipeline, bindings[flip]);
  pass(encoder, context.getCurrentTexture().createView(), presentPipeline, bindings[1 - flip]);
  device.queue.submit([encoder.finish()]);
  flip = 1 - flip;
  const t2 = performance.now();
  capture?.();   // export: grab the canvas in the same task that drew it
  const done = afterPaint;
  afterPaint = null;
  device.queue.onSubmittedWorkDone().then(() => {
    done?.();
  });
};

// Each arranged scene gets its own clock: age counts from its note, and a scene that declares
// `bars` in its manifest gets progress 0 to 1 across them.
function driveArranged(next) {
  for (const layer of arrangedLayers) {
    layer.state = layerAt(layer.notes.filter((n) => layer.sceneForNote.has(n.note)), next.beat, layer.cutBeats);
    for (const note of [layer.state?.from, layer.state?.to]) {
      if (!note) continue;
      const scene = layer.sceneForNote.get(note.note);
      if (layer.macros) applyMacros(scene, layer.macros);   // each layer's knobs are its own track's
      const age = next.beat - note.on;
      const storyBeats = (scene.manifest.bars ?? 0) * next.sig[0];
      drive(scene, { ...next, age, progress: storyBeats ? Math.min(1, age / storyBeats) : 0 });
    }
  }
}
const layerState = ({ state }) => ({ from: state?.from?.note ?? null, to: state?.to.note ?? null, progress: Number((state?.progress ?? 1).toFixed(4)) });
window.arrangementAt = () => ({ beat: Number(frame.beat.toFixed(3)), ...layerState(arrangedLayers[0]) });
window.layersAt = () => ({ beat: Number(frame.beat.toFixed(3)), layers: arrangedLayers.map(layerState) });

function render(next) {
  frame = next;
  if (set) driveArranged(next);
  else for (const scene of scenes) drive(scene, frame);
  requested = true;
  canvas.requestPaint();
}

// ---- live mode: wall clock, as the plugin would drive it ----
let live = false;
let lastTick = 0;
let liveStart = 0;
// With ?live=port every Stage Link instance arrives through the app: one link per track. The page
// asks its parent for the port, and reports what is playing back to it.
const app = query.get("live") === "port" ? window.parent : null;
const linePort = app && (await new Promise((resolve) => {
  addEventListener("message", (event) => event.ports[0] && resolve(event.ports[0]), { once: true });
  app.postMessage({ type: "port?" }, "*");
}));
const hub = linePort ? connectHub({ onLine: (handler) => { linePort.onmessage = (event) => handler(event.data); } }) : null;
// What each layer is showing, and the knobs its scene declares, for the app's rail and knob strip.
let lastState = "";
function reportState(next) {
  const layers = arrangedLayers.map((layer) => {
    const playing = layer.state?.to && layer.sceneForNote.get(layer.state.to.note);
    return { from: layer.state?.from?.note ?? null, to: layer.state?.to?.note ?? null, knobs: (playing?.manifest.params ?? []).slice(0, 8).map((p) => p.name) };
  });
  const state = JSON.stringify(layers);
  if (state === lastState) return;
  lastState = state;
  app.postMessage({ type: "state", layers, beat: next.beat }, "*");
}
const silent = { hits: [], block: null };
let liveFrames = 0;
let startBeat = null;
// One frame from the tracks heard by `now`, live or from a take. Playing tracks come first, then
// the order they connected in, so a stopped DAW never takes the clock from a playing one and two
// tracks claiming one layer resolve the same way every frame. Drums come from the drums track;
// each layer's notes and knobs from its own track.
function trackFrame(links, now, n, dt, clockId) {
  const tracks = alive(links, now).sort((a, b) => Number(b.block.playing) - Number(a.block.playing));
  const clock = links.get(clockId) ?? tracks[0];
  if (!clock) return null;
  const next = frameFromLink(clock, { n, dt, width: W, height: H, startBeat: startBeat ?? 0, now }, tracks.find((track) => track.role === 0) ?? silent);
  startBeat ??= next.beat;
  arrangedLayers.forEach((layer, i) => {
    const track = tracks.find((candidate) => candidate.role === i + 1);
    layer.notes = track?.arrangement ?? [];
    layer.macros = track?.block.macros;
  });
  return next;
}

// A take replays as the same lines, delivered against the take's own clock instead of the wall
// clock. With sound, that clock is the sound's sample count: frame n is sample n * rate / fps, and
// the sound track's blocks say which beat each sample falls on. Without sound it is the plugins'
// own timestamps. Frames must be asked for in order, starting at 0.
function takeFrame(n, fps) {
  if (n === 0) {
    take.cursor = 0;
    take.links = new Map();
    startBeat = null;
  }
  const { sound } = take.header;
  const sample = sound ? sound.frame + Math.round((n * sound.rate) / fps) : 0;
  const micros = take.lines[0].us + (n / fps) * 1e6;
  for (; take.cursor < take.lines.length; take.cursor += 1) {
    const line = take.lines[take.cursor];
    if (sound ? take.links.has(sound.id) && line.id === sound.id && line.frame > sample : line.us > micros) break;
    deliver(take.links, line, line.us);
  }
  const block = sound && take.links.get(sound.id).block;
  return trackFrame(take.links, sound ? block.us + ((sample - block.frame) / sound.rate) * 1e6 : micros, n, 1 / fps, sound?.id);
}

function tick(now) {
  if (!live) return;
  const dt = lastTick ? (now - lastTick) / 1000 : 1 / 60;
  lastTick = now;
  // With no track heard, a built-in drum loop keeps the picture moving, and a pad clicked in the app
  // holds its scene, so a set can be tried without a DAW.
  let next = hub && trackFrame(hub, nowMicros(), liveFrames, dt);
  if (!next) {
    next = frameAtTime((now - liveStart) / 1000, { bpm: 120, width: W, height: H });
    arrangedLayers.forEach((layer, i) => {
      layer.notes = audition?.layer === i ? [{ note: audition.note, on: audition.beat ?? (audition.beat = next.beat), off: Infinity }] : [];
      layer.macros = undefined;
    });
  }
  liveFrames += 1;
  render(next);
  if (app) reportState(next);
  requestAnimationFrame(tick);
}

// The app's side of the page: { type: "audition", layer, note } holds a scene while nothing is connected.
let audition = null;
addEventListener("message", (event) => { if (event.data?.type === "audition") audition = event.data.note === null ? null : { layer: event.data.layer, note: event.data.note }; });

// For tests: the tracks heard now.
window.hubStats = () => alive(hub).map((track) => ({ role: track.role, playing: track.block.playing, firstKnob: track.block.macros[0], held: track.block.held.map(([note]) => note) })).sort((a, b) => a.role - b.role);

// ---- step mode: a stepped clock, as export would drive it ----
window.stepTo = (n, fps = 60, bpm = 174) =>
  new Promise((resolve) => {
    afterPaint = resolve;
    render(frameAt(n, { fps, bpm, width: W, height: H }));
  });

window.stepTake = (n, fps = 60) =>
  new Promise((resolve) => {
    afterPaint = resolve;
    render(takeFrame(n, fps));
  });

// ---- export, whole: stepped clock -> Mediabunny (WebCodecs inside) -> MP4 with an audio track ----
async function mux({ frames, fps, name, audioBuffer, frameFor }) {
  const { Output, Mp4OutputFormat, BufferTarget, CanvasSource, AudioBufferSource } = await import("./vendor/mediabunny.min.mjs");
  const output = new Output({ format: new Mp4OutputFormat(), target: new BufferTarget() });
  const video = new CanvasSource(canvas, { codec: "avc", bitrate: 20e6, keyFrameInterval: 1 });
  const audio = audioBuffer && new AudioBufferSource({ codec: "aac", bitrate: 192e3 });
  output.addVideoTrack(video, { frameRate: fps });
  if (audio) output.addAudioTrack(audio);
  await output.start();
  await audio?.add(audioBuffer);

  const started = performance.now();
  for (let n = 0; n < frames; n += 1) {
    let added;
    await new Promise((resolve) => {
      afterPaint = resolve;
      capture = () => { capture = null; added = video.add(n / fps, 1 / fps); };
      render(frameFor(n));
    });
    await added;
  }
  await output.finalize();
  const seconds = (performance.now() - started) / 1000;
  const bytes = output.target.buffer;
  await fetch(`/upload?name=${encodeURIComponent(name)}`, { method: "POST", body: bytes });
  return { name, frames, megabytes: Number((bytes.byteLength / 1e6).toFixed(2)), framesPerSecond: Number((frames / seconds).toFixed(1)) };
}

// A recorded take to video: its lines drive the picture and its own sound goes in untouched.
window.exportTake = async ({ fps, name }) => {
  const { sound } = take.header;
  const wav = sound && (await (await fetch(new URL(sound.file, new URL(take.url, location.href)))).arrayBuffer());
  const audioBuffer = wav && (await new OfflineAudioContext(sound.channels, 1, sound.rate).decodeAudioData(wav));
  const seconds = audioBuffer ? audioBuffer.duration : (take.lines.at(-1).us - take.lines[0].us) / 1e6;
  return mux({ frames: Math.floor(seconds * fps), fps, name, audioBuffer, frameFor: (n) => takeFrame(n, fps) });
};

// Hash of the last composited frame, read back from the GPU.
window.hashFrame = async () => {
  const bytesPerRow = Math.ceil((W * 4) / 256) * 256;
  const buffer = device.createBuffer({ size: bytesPerRow * H, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
  const encoder = device.createCommandEncoder();
  encoder.copyTextureToBuffer({ texture: targets[flip] }, { buffer, bytesPerRow }, [W, H]);
  device.queue.submit([encoder.finish()]);
  await buffer.mapAsync(GPUMapMode.READ);
  const bytes = new Uint8Array(buffer.getMappedRange());
  let hash = 0x811c9dc5;
  let lit = 0;
  for (let y = 0; y < H; y += 1) {
    for (let x = 0; x < W * 4; x += 1) {
      const value = bytes[y * bytesPerRow + x];
      hash = Math.imul(hash ^ value, 0x01000193) >>> 0;
      if (x % 4 !== 3 && value > 24) lit += 1;
    }
  }
  buffer.unmap();
  buffer.destroy();
  return { hash: hash.toString(16), litFraction: Number((lit / (W * H * 3)).toFixed(4)) };
};

report.layers = scenes.map((scene) => ({
  url: scene.url,
  errors: scene.errors,
}));
report.ready = true;
if (hub) {
  live = true;
  liveStart = performance.now();
  requestAnimationFrame(tick);
}
