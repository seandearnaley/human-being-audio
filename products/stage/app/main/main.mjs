// Stage, the app. One window. This process owns everything with a privilege: the hub the plugins
// connect to, the files on disk, the agent and its key, recording and export.
//
// Two origins keep those privileges away from scene files, which other people or a model wrote:
//   stage://app/      the app's own page. It alone gets the bridge in preload.cjs.
//   stage://output/   the picture: the engine page and, under /home/, the person's files. It runs
//                     in a frame inside the app's page, and has no bridge at all.
//
// A scene shares the output origin with the engine page, so it can use that page's fetch and read
// the person's files. What it must not do is send them anywhere. So no page in this app can reach
// the network at all: every request that is not stage:// is cancelled, and no frame may navigate
// away or open a window. The only thing that talks to the network is the agent, from this process.
import { app, BrowserWindow, ipcMain, protocol, session, shell } from "electron";
import fs from "node:fs/promises";
import path from "node:path";
import { startHub } from "./hub.mjs";
import { openLibrary } from "./library.mjs";
import { createChecker, createCutChecker } from "./check.mjs";
import { writeFile, SCENE_JOB, CUT_JOB } from "./agent.mjs";
import { exportTake } from "./export.mjs";

const ROOT = path.join(import.meta.dirname, "..");
const OUTPUT = "stage://output";
const TYPES = {
  ".html": "text/html; charset=utf-8", ".js": "text/javascript", ".mjs": "text/javascript", ".css": "text/css", ".json": "application/json",
  ".wgsl": "text/plain", ".jpg": "image/jpeg", ".png": "image/png", ".svg": "image/svg+xml", ".wav": "audio/wav", ".take": "text/plain", ".woff2": "font/woff2",
};
// Scenes get no network and nothing external. A scene can only draw.
const SCENE_POLICY = "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src data: blob:; font-src data:; connect-src 'none'";
// The engine pages get a policy too. The request filter below is what stops the network; this is a
// second lock on the same door.
const ENGINE_POLICY = "default-src 'self' data: blob: 'unsafe-inline' 'wasm-unsafe-eval'; connect-src 'self' data: blob:";
const MODEL = process.env.STAGE_MODEL ?? "claude-opus-5-5";
// In development the app's own page comes from Vite on localhost. Nothing else is ever allowed.
const DEV = process.env.STAGE_DEV_URL ? new URL(process.env.STAGE_DEV_URL) : null;
const allowed = (address) => {
  const url = new URL(address);
  return ["stage:", "data:", "blob:", "devtools:", "about:"].includes(url.protocol) || (DEV !== null && url.host === DEV.host);
};

app.setName("Stage");
protocol.registerSchemesAsPrivileged([{ scheme: "stage", privileges: { standard: true, secure: true, supportFetchAPI: true, stream: true } }]);

async function serve(file, headers = {}) {
  const body = file && (await fs.readFile(file).catch(() => null));
  if (!body) return new Response("Not found", { status: 404 });
  return new Response(body, { headers: { "Content-Type": TYPES[path.extname(file)] ?? "application/octet-stream", "Cache-Control": "no-store", ...headers } });
}

app.on("web-contents-created", (_event, contents) => {
  contents.setWindowOpenHandler(() => ({ action: "deny" }));
  // WebRTC is not an ordinary request, so the filter never sees it. This makes it use the proxy
  // set below, which goes nowhere.
  contents.setWebRTCIPHandlingPolicy("disable_non_proxied_udp");
  contents.on("will-frame-navigate", (event) => allowed(event.url) || event.preventDefault());
});

app.whenReady().then(async () => {
  session.defaultSession.webRequest.onBeforeRequest((details, respond) => respond({ cancel: !allowed(details.url) }));
  // A proxy that is not there: anything that slips past the filter has nowhere to go. stage:// is
  // served by this process and never touches it. Chromium leaves localhost unproxied, which is
  // what development needs.
  await session.defaultSession.setProxy({ proxyRules: "127.0.0.1:9" });
  const library =await openLibrary(process.env.STAGE_HOME ?? path.join(app.getPath("music"), "Stage"), path.join(ROOT, "starter"));
  let setFile = await library.lastSet();
  let set = await library.readSet(setFile);
  const open = async (name) => {
    if (!(await library.sets()).includes(name)) throw new Error("no such set");
    setFile = name;
    set = await library.readSet(name);
    await library.rememberSet(name);
    return { file: setFile, ...set };
  };
  let expectedUpload = null;   // the one file an export in progress may write

  protocol.handle("stage", async (request) => {
    const url = new URL(request.url);
    if (url.host === "app") return serve(path.join(ROOT, "dist", url.pathname === "/" ? "index.html" : url.pathname));
    if (url.host !== "output") return new Response("Not found", { status: 404 });
    if (request.method === "POST" && url.pathname === "/upload") {
      const name = url.searchParams.get("name");
      if (!expectedUpload || name !== expectedUpload) return new Response("Not expected", { status: 403 });
      expectedUpload = null;
      await fs.writeFile(library.file("exports", name), Buffer.from(await request.arrayBuffer()));
      return new Response(null, { status: 204 });
    }
    if (url.pathname.startsWith("/home/")) return serve(library.resolve(url.pathname), url.pathname.endsWith(".html") ? { "Content-Security-Policy": SCENE_POLICY } : {});
    if (url.pathname === "/vendor/mediabunny.min.mjs") return serve(path.join(ROOT, "node_modules/mediabunny/dist/bundles/mediabunny.min.mjs"));
    return serve(path.join(ROOT, "engine", path.normalize(url.pathname)), url.pathname.endsWith(".html") ? { "Content-Security-Policy": ENGINE_POLICY } : {});
  });

  const window = new BrowserWindow({
    width: 1440, height: 900, minWidth: 1180, minHeight: 760, backgroundColor: "#0f0e14", titleBarStyle: "hiddenInset", show: process.env.STAGE_HIDDEN !== "1",
    // The Stage usually sits behind a DAW. It must keep its frame rate there.
    webPreferences: { preload: path.join(import.meta.dirname, "preload.cjs"), backgroundThrottling: false },
  });
  const hub = await startHub(path.join(app.getPath("userData"), "hub.json"), (line) => window.isDestroyed() || window.webContents.send("line", line));
  const checkScene = createChecker(OUTPUT);

  const handlers = {
    "set:get": () => ({ file: setFile, ...set }),
    // Every set, by file, with the name inside it.
    "set:list": async () => Promise.all((await library.sets()).map(async (file) => ({ file, name: (await library.readSet(file)).name }))),
    "set:open": ({ file }) => open(file),
    "set:new": async () => open(await library.createSet()),
    async "set:rename"({ name }) {
      set.name = name.trim() || "Untitled";
      await library.writeSet(setFile, set);
      return { file: setFile, ...set };
    },
    "library:list": () => library.list(),
    // Put a scene on a pad: the note given, or the lowest free one from C3 up.
    async "set:assign"({ layer, note, url }) {
      const scenes = set.layers[layer].scenes;
      let target = note ?? 60;
      while (note === undefined && scenes[target]) target += 1;
      scenes[target] = url;
      await library.writeSet(setFile, set);
      return { file: setFile, ...set };
    },
    async "set:choose"({ cut, look }) {
      if (cut !== undefined) set.cut = cut;
      if (look !== undefined) set.look = look;
      await library.writeSet(setFile, set);
      return { file: setFile, ...set };
    },
    // The agent. What is selected decides what it writes: nothing, a new scene; one scene, a
    // change to it; two scenes, a Cut between them.
    async "agent:make"({ request, change, pair }, sender) {
      if (!process.env.ANTHROPIC_API_KEY) return { error: "Stage needs an Anthropic key to make things. Set ANTHROPIC_API_KEY and start it again." };
      const kind = pair ? "cuts" : "scenes";
      const stem = `${request.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "").slice(0, 28)}-${Date.now().toString(36).slice(-4)}`;
      const file = library.file("library", kind, stem + library.kinds[kind]);
      const url = library.address("library", kind, stem + library.kinds[kind]);
      let check = checkScene;
      if (pair) {
        // The Cut is played between the two scenes selected, on a set made for the purpose.
        await fs.writeFile(library.file(".drafts", "pair.json"), JSON.stringify({ cutBeats: 2, scenes: { 60: pair[0], 62: pair[1] }, clip: [[60, 0, 6], [62, 4, 12]] }));
        check = createCutChecker(OUTPUT, library.address(".drafts", "pair.json"));
      }
      try {
        const result = await writeFile({
          model: MODEL, request, file, url, check, field: pair ? "wgsl" : "html",
          instructions: (await fs.readFile(path.join(ROOT, "skill", pair ? "CUT.md" : "SCENE.md"), "utf8")) + (pair ? CUT_JOB : SCENE_JOB),
          existing: change && (await fs.readFile(library.resolve(change), "utf8")),
          onEvent: (event) => sender.isDestroyed() || sender.send("agent:event", event),
        });
        const picture = result.pictures[pair ? 2 : 0];
        if (picture) await fs.writeFile(file.replace(/\.\w+$/, ".jpg"), Buffer.from(picture, "base64"));
        return { url, kind, passed: result.passed, said: result.said, seconds: result.seconds, saves: result.attempts.length, checks: result.attempts.at(-1) ?? null };
      } catch (error) {
        await fs.rm(file, { force: true });
        return { error: String(error.message ?? error) };
      } finally {
        check.dispose();   // its hidden window; the next job opens a fresh one
      }
    },
    async "agent:discard"({ url }) {
      const file = library.resolve(url);
      if (file) await Promise.all([fs.rm(file, { force: true }), fs.rm(file.replace(/\.\w+$/, ".jpg"), { force: true })]);
    },
    // A take is named for when it started. Its header carries the set as it was at that moment, so
    // a later change to the set does not change what the take exports.
    async "take:record"({ soundId }) {
      const stamp = new Date();
      const pad = (value) => String(value).padStart(2, "0");
      const name = `${stamp.getFullYear()}-${pad(stamp.getMonth() + 1)}-${pad(stamp.getDate())} ${pad(stamp.getHours())}.${pad(stamp.getMinutes())}.${pad(stamp.getSeconds())}`;
      await hub.record(library.file("takes"), name, soundId, { set: { file: setFile, ...set } });
      return { name };
    },
    "take:finish": () => hub.finish(),
    "take:list": () => library.takes(),
    async "take:export"({ take, height, fps }) {
      const header = JSON.parse((await fs.readFile(library.file("takes", `${take}.take`), "utf8")).split("\n", 1)[0]);
      // Takes made before the set was stored in the header only name its file.
      const played = header.set && typeof header.set === "object" ? header.set : await library.readSet(header.set ?? setFile);
      await fs.writeFile(library.file(".drafts", "take-set.json"), JSON.stringify(played));
      const name = `${take} ${height}p${fps}.mp4`;
      expectedUpload = name;
      try {
        const result = await exportTake({
          origin: OUTPUT, name, fps, height, width: Math.round((height * 16) / 9), look: Boolean(played.look),
          takeUrl: library.address("takes", `${take}.take`), setUrl: library.address(".drafts", "take-set.json"),
        });
        return { file: library.file("exports", name), ...result };
      } catch (error) {
        return { error: String(error.message ?? error) };
      } finally {
        expectedUpload = null;
      }
    },
    reveal: ({ file }) => shell.showItemInFolder(file),
  };
  for (const [channel, handler] of Object.entries(handlers)) ipcMain.handle(channel, (event, payload) => handler(payload ?? {}, event.sender));

  // One window: when it closes the app is done, whatever hidden windows a job may still hold.
  window.on("closed", () => app.quit());
  app.on("will-quit", () => hub.stop());
  await window.loadURL(process.env.STAGE_DEV_URL ?? "stage://app/");
});
