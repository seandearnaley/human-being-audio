// The arrangement rule: a note is a scene, and a note that starts while the last is held is a Cut.
import test from "node:test";
import assert from "node:assert/strict";
import { layerAt } from "../engine/arrangement.js";

const notes = [{ note: 60, on: 0, off: 14 }, { note: 62, on: 12, off: 24 }];

test("nothing before the first note, and nothing after the last ends", () => {
  assert.equal(layerAt(notes, -1, 2), null);
  assert.equal(layerAt(notes, 30, 2), null);
});
test("a held note is its scene", () => {
  assert.deepEqual(layerAt(notes, 5, 2), { from: null, to: notes[0], progress: 1 });
});
test("an overlap is a cut of the declared length, whatever the overlap's own length", () => {
  const cut = layerAt(notes, 12.5, 2);
  assert.equal(cut.from, notes[0]);
  assert.equal(cut.to, notes[1]);
  assert.equal(cut.progress, 0.25);
  assert.deepEqual(layerAt(notes, 14.5, 2), { from: null, to: notes[1], progress: 1 });
});
test("no overlap is a hard cut", () => {
  const apart = [{ note: 60, on: 0, off: 4 }, { note: 62, on: 4, off: 8 }];
  assert.deepEqual(layerAt(apart, 4.1, 2), { from: null, to: apart[1], progress: 1 });
});
test("a note still held shows, however long ago it began", () => {
  assert.equal(layerAt([{ note: 60, on: 0, off: Infinity }], 500, 2).to.note, 60);
});
