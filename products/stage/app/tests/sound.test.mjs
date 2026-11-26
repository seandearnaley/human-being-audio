// Which track a take records its sound from.
import test from "node:test";
import assert from "node:assert/strict";
import { pickSound } from "../src/sound.ts";

const link = (id, playing = true) => ({ id, role: 1, playing, sound: true });
const mix = (id, playing = true) => ({ id, role: 5, playing, sound: true });

test("no track with sound means no sound", () => {
  assert.equal(pickSound([{ id: "a", role: 1, playing: true, sound: false }]), null);
});
test("Stage Sound wins over a sidechain, whichever connected first", () => {
  assert.equal(pickSound([link("sidechain"), mix("mix")]), "mix");
  assert.equal(pickSound([mix("mix"), link("sidechain")]), "mix");
});
test("a sidechain is used when there is no Stage Sound", () => {
  assert.equal(pickSound([link("first"), link("second")]), "first");
});
test("a playing sidechain wins over a stopped DAW's Stage Sound", () => {
  assert.equal(pickSound([mix("idle", false), link("live")]), "live");
});
