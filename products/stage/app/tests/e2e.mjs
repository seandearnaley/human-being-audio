// End to end, in the real app: start it on an empty home folder, look at what it shows, then drive
// it. Screenshots go to out/. With HOST=<path to stage-link-host> it also plays three tracks into
// the hub; with DAW=1 it expects a real DAW to be playing them instead. With AGENT=1 it asks the
// agent for a scene (this spends on the Anthropic key).
import { _electron as electron } from "playwright";
import { spawn } from "node:child_process";
import dgram from "node:dgram";
import fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";

const here = path.join(import.meta.dirname, "..");
const out = path.join(here, "out");
const home = await fs.mkdtemp(path.join(os.tmpdir(), "stage-home-"));
await fs.mkdir(out, { recursive: true });

const app = await electron.launch({ args: [here], env: { ...process.env, STAGE_HOME: home } });
const page = await app.firstWindow();
const problems = [];
// The network checks below try to reach example.com on purpose; the refusals they log are the point.
page.on("console", (message) => message.type() === "error" && !message.text().includes("example.com") && problems.push(message.text()));
page.on("pageerror", (error) => problems.push(String(error)));
const shot = (name) => page.screenshot({ path: path.join(out, `${name}.png`) });
const report = {};

try {
  await page.waitForSelector(".pad img");
  await page.waitForTimeout(2500);
  report.pads = await page.locator(".pad img").count();
  report.outputErrors = await page.frames().find((frame) => frame.url().startsWith("stage://output"))?.evaluate(() => window.report?.errors);
  await shot("01-idle");

  // A scene can borrow the engine page's fetch. Neither may reach the network, by any road.
  // WebRTC is the road a request filter cannot see, so a local UDP listener stands in for a STUN
  // server: if a single datagram arrives, WebRTC got out.
  const stun = dgram.createSocket("udp4");
  let datagrams = 0;
  stun.on("message", () => { datagrams += 1; });
  await new Promise((resolve) => stun.bind(0, "127.0.0.1", resolve));
  report.network = await page.frames().find((frame) => frame.url().startsWith("stage://output")).evaluate(async (stunPort) => {
    const tried = (attempt) => attempt.then(() => "reached", () => "blocked");
    const scene = [...document.querySelectorAll("iframe")][0].contentWindow;
    const peer = new RTCPeerConnection({ iceServers: [{ urls: `stun:127.0.0.1:${stunPort}` }] });
    peer.createDataChannel("out");
    await peer.setLocalDescription(await peer.createOffer());
    await new Promise((resolve) => setTimeout(resolve, 2500));
    peer.close();
    return {
      engineFetch: await tried(fetch("https://example.com/", { mode: "no-cors" })),
      sceneViaParent: await tried(scene.parent.fetch("https://example.com/", { mode: "no-cors" })),
      image: await new Promise((resolve) => { const image = new Image(); image.onload = () => resolve("reached"); image.onerror = () => resolve("blocked"); image.src = "https://example.com/favicon.ico"; }),
      ownFiles: await tried(fetch("/home/sets/night-drive.json").then((response) => response.ok || Promise.reject())),
    };
  }, stun.address().port);
  report.network.webrtc = datagrams ? "reached" : "blocked";
  stun.close();

  // No plugin connected: a pad holds its scene over the built-in loop.
  await page.locator(".pad").first().click();
  await page.waitForTimeout(1500);
  await shot("02-audition");
  report.showingAfterClick = await page.locator(".layer-name .now").first().textContent();

  if (process.env.HOST || process.env.DAW) {
    const host = process.env.HOST ? spawn(process.env.HOST, ["16", "120", "256", "tracks"], { stdio: "ignore" }) : null;
    await page.waitForSelector(".link.on", { timeout: 8000 });
    report.connected = true;
    const seen = [];
    for (let i = 0; i < 26; i += 1) {
      await page.waitForTimeout(500);
      const now = await page.locator(".layer-name .now").allTextContents();
      const key = `${await page.locator(".position").textContent()} ${now.join(" | ")}`;
      if (seen.at(-1)?.split(" ").slice(3).join(" ") !== now.join(" | ")) seen.push(key);
      if (i === 8) await shot("03-live");
      // The drum lights flash for about a tenth of a second: sample them fast for two seconds.
      if (i === 10) { report.drumLights = 0; for (let n = 0; n < 40; n += 1) { report.drumLights += await page.locator(".hit.on").count(); await page.waitForTimeout(50); } }
      if (i === 4) { await page.locator("nav button", { hasText: "Record" }).click(); report.recording = true; }
      if (i === 20) await page.locator("nav button[aria-pressed=true]").first().click();
    }
    report.seen = seen;
    report.knobs = await page.locator(".knob code").allTextContents();
    host?.kill();
    await page.waitForTimeout(1500);
    report.takes = (await fs.readdir(path.join(home, "takes"))).sort();
    // The take carries the set as it was played, not just its file name.
    const header = JSON.parse((await fs.readFile(path.join(home, "takes", report.takes[0]), "utf8")).split("\n", 1)[0]);
    report.takeCarriesSet = { file: header.set.file, name: header.set.name, layers: header.set.layers.length, sound: header.sound?.rate ?? null };

    await page.locator("nav button", { hasText: "Export" }).click();
    await page.waitForSelector(".sheet .choice");
    await shot("04-export");
    await page.locator(".sheet button.primary").click();
    await page.waitForSelector(".sheet button.primary:has-text('Show in Finder')", { timeout: 120000 });
    report.exported = await page.locator(".sheet .row code").textContent();
    report.exports = await fs.readdir(path.join(home, "exports"));
    // Keep the video and the take beside the screenshots; the home folder is thrown away.
    await fs.copyFile(path.join(home, "exports", report.exports[0]), path.join(out, "export.mp4"));
    for (const name of report.takes) await fs.copyFile(path.join(home, "takes", name), path.join(out, `take${path.extname(name)}`));
    await shot("05-exported");
    await page.locator(".sheet button", { hasText: "Done" }).click();
  }

  await page.locator("nav button", { hasText: "Library" }).click();
  await page.waitForSelector(".tile img");
  await shot("06-library");
  report.tiles = await page.locator(".tile").count();
  await page.locator("nav button", { hasText: "Library" }).click();

  if (process.env.AGENT) {
    await page.locator(".pad").first().click();   // unpick
    await page.locator(".prompt input").fill("Slow concentric rings in deep red that pulse outward on every kick.");
    await page.keyboard.press("Enter");
    await page.waitForSelector(".making .working");
    await shot("07-making");
    await page.waitForSelector(".making .actions .primary", { timeout: 240000 });
    await page.waitForTimeout(2500);
    report.made = await page.locator(".making").innerText();
    await shot("08-made");
    await page.locator(".making .actions .primary").click();
    await page.waitForTimeout(2500);
    report.padsAfterKeep = await page.locator(".pad img").count();
    // The agent's hidden check window is gone once the job is done.
    report.windowsAfterAgent = await app.evaluate(({ BrowserWindow }) => BrowserWindow.getAllWindows().length);
    await shot("09-kept");
  }
  // Sets: make one, name it, fill a pad from the library, and switch back.
  await page.locator(".set > button").click();
  await page.locator(".menu button", { hasText: "New set" }).click();
  await page.waitForFunction(() => document.activeElement?.getAttribute("aria-label") === "Set name" && document.activeElement.value === "Untitled" && document.activeElement.selectionEnd === 8);
  await page.keyboard.type("Warehouse");
  await page.keyboard.press("Enter");
  await page.waitForTimeout(800);
  report.newSet = { name: await page.locator(".set input").inputValue(), pads: await page.locator(".pad img").count(), files: (await fs.readdir(path.join(home, "sets"))).sort() };
  await shot("10-new-set");
  await page.locator("button.pad.empty").nth(4).click();          // an empty pad of the second layer opens the library
  await page.locator(".tile").first().click();
  await page.waitForSelector(".pad img");
  report.newSet.afterAdding = await page.locator(".layer").nth(1).locator(".pad img").count();
  await page.locator(".set > button").click();
  await page.waitForSelector(".menu button");
  report.menu = await page.locator(".menu button").allTextContents();
  await shot("11-set-menu");
  await page.locator(".menu button", { hasText: "Night Drive" }).click();
  await page.waitForTimeout(800);
  report.backOnFirst = { name: await page.locator(".set input").inputValue(), pads: await page.locator(".pad img").count() };
  await page.locator(".set > button").click();
  await page.locator(".menu button", { hasText: "Warehouse" }).click();
  await page.waitForTimeout(500);
} catch (error) {
  report.failed = String(error);
  await shot("failed").catch(() => {});
}
report.problems = problems;
await app.close();

// The app comes back to the set that was open.
const again = await electron.launch({ args: [here], env: { ...process.env, STAGE_HOME: home } });
const reopened = await again.firstWindow();
await reopened.waitForSelector(".set input");
await reopened.waitForTimeout(800);
report.reopenedOn = { name: await reopened.locator(".set input").inputValue(), pads: await reopened.locator(".pad img").count() };
await again.close();

console.log(JSON.stringify(report, null, 1));
await fs.rm(home, { recursive: true, force: true });
