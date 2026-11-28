// The library is a folder. These check that the starter lands on first run and that nothing
// outside the folder can be asked for.
import test from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { openLibrary } from "../main/library.mjs";

const starter = path.join(import.meta.dirname, "../starter");
const fresh = async () => openLibrary(await fs.mkdtemp(path.join(os.tmpdir(), "stage-test-")), starter);

test("first run copies the starter set, and every scene it names exists", async () => {
  const library = await fresh();
  const [name] = await library.sets();
  const set = await library.readSet(name);
  assert.equal(set.name, "Night Drive");
  for (const layer of set.layers) for (const url of Object.values(layer.scenes)) await fs.access(library.resolve(url));
  await fs.access(library.resolve(set.cut));
  await fs.access(library.resolve(set.look));
});
test("a second open does not overwrite the person's set", async () => {
  const library = await fresh();
  const [name] = await library.sets();
  await library.writeSet(name, { name: "Mine", layers: [] });
  assert.equal((await (await openLibrary(library.home, starter)).readSet(name)).name, "Mine");
});
test("scenes are listed with the name they declare and their picture", async () => {
  const { scenes, cuts } = await (await fresh()).list();
  assert.ok(scenes.length >= 6);
  assert.ok(scenes.every((scene) => scene.name && scene.thumb?.endsWith(".jpg")));
  assert.deepEqual(cuts.map((cut) => cut.name).sort(), ["Blinds", "Glitch", "Push"]);
});
test("an address cannot leave the folder", async () => {
  const library = await fresh();
  assert.equal(library.resolve("/home/../../etc/passwd"), null);
  assert.equal(library.resolve("/home/library/../../../x"), null);
  assert.equal(library.resolve("/home/%2e%2e/%2e%2e/x"), null);
  assert.ok(library.resolve("/home/library/scenes/a.html").startsWith(library.home));
});
test("a new set is empty, never reuses a file name, and is remembered once opened", async () => {
  const library = await fresh();
  const first = await library.createSet();
  const second = await library.createSet();
  assert.notEqual(first, second);
  const set = await library.readSet(first);
  assert.equal(set.name, "Untitled");
  assert.deepEqual(set.layers.map((layer) => Object.keys(layer.scenes).length), [0, 0]);
  await fs.access(library.resolve(set.cut));
  assert.equal(await library.lastSet(), "night-drive.json");
  await library.rememberSet(second);
  assert.equal(await library.lastSet(), second);
  await fs.rm(library.file("sets", second));
  assert.equal(await library.lastSet(), "night-drive.json");   // a remembered set that is gone falls back
});
