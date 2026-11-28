import assert from "node:assert/strict";
import { openSync, readSync, closeSync, statSync } from "node:fs";
import path from "node:path";
import { test } from "node:test";
import { PRODUCT_SLUGS } from "../lib/catalog.mjs";
import { PRODUCT_MEDIA } from "../lib/media.mjs";

const PUBLIC = path.resolve(import.meta.dirname, "../public");
const VIDEO_BUDGET = 12 * 1024 * 1024; // per encode; the page only fetches one, and only on play
const IMAGE_BUDGET = 400 * 1024;

const file = (src) => path.join(PUBLIC, src);

function head(src, bytes = 32) {
  const fd = openSync(file(src), "r");
  const buf = Buffer.alloc(bytes);
  readSync(fd, buf, 0, bytes, 0);
  closeSync(fd);
  return buf;
}

// Width and height from a WebP's VP8, VP8L or VP8X header.
function webpSize(src) {
  const b = head(src);
  assert.equal(b.toString("ascii", 0, 4), "RIFF", src);
  assert.equal(b.toString("ascii", 8, 12), "WEBP", src);
  const chunk = b.toString("ascii", 12, 16);
  if (chunk === "VP8X") return [1 + b.readUIntLE(24, 3), 1 + b.readUIntLE(27, 3)];
  if (chunk === "VP8L") {
    const bits = b.readUInt32LE(21);
    return [(bits & 0x3fff) + 1, ((bits >> 14) & 0x3fff) + 1];
  }
  assert.equal(chunk, "VP8 ", src);
  return [b.readUInt16LE(26) & 0x3fff, b.readUInt16LE(28) & 0x3fff];
}

test("every product media entry points at real, correctly sized, budgeted files", () => {
  for (const [slug, media] of Object.entries(PRODUCT_MEDIA)) {
    assert.ok(PRODUCT_SLUGS.includes(slug), `Unknown product ${slug}`);
    const { video, gallery = [] } = media;
    if (video) {
      assert.ok(video.title && video.badge && video.caption && Number.isInteger(video.seconds) && video.seconds > 0);
      assert.deepEqual(webpSize(video.poster), [video.width, video.height]);
      assert.ok(statSync(file(video.poster)).size <= IMAGE_BUDGET, video.poster);
      assert.deepEqual(
        video.sources.map((s) => path.extname(s.src)),
        [".webm", ".mp4"],
        "WebM first, MP4 as the universal fallback",
      );
      for (const { src, type } of video.sources) {
        assert.ok(src.startsWith(`/media/${slug}/`) && type.startsWith("video/"), src);
        assert.ok(statSync(file(src)).size <= VIDEO_BUDGET, `${src} is over budget`);
      }
      assert.equal(head(video.sources[1].src).toString("ascii", 4, 8), "ftyp");
      assert.deepEqual([...head(video.sources[0].src, 4)], [0x1a, 0x45, 0xdf, 0xa3]); // EBML
    }
    const alts = new Set();
    for (const shot of gallery) {
      assert.ok(shot.src.startsWith(`/media/${slug}/gallery/`), shot.src);
      assert.deepEqual(webpSize(shot.src), [shot.width, shot.height], shot.src);
      // Full views stay Retina-sharp in the 1600 px lightbox; details are 1:1 crops of the interface.
      assert.ok(shot.width >= (shot.detail ? 1920 : 2560), `${shot.src} is too small to stay sharp`);
      assert.ok(statSync(file(shot.src)).size <= IMAGE_BUDGET, shot.src);
      assert.ok(shot.label && shot.alt.length > 40, `${shot.src} needs a label and descriptive alt text`);
      assert.ok(!alts.has(shot.alt), `Duplicate alt text in ${slug}`);
      alts.add(shot.alt);
    }
  }
});

test("Livecut Redux and Tract Form ship a 30-60 s demo video and an eight-shot gallery", () => {
  for (const slug of ["livecut-redux", "tract-form"]) {
    const { video, gallery } = PRODUCT_MEDIA[slug];
    assert.ok(video.seconds >= 30 && video.seconds <= 60, slug);
    assert.equal(gallery.length, 8, slug);
  }
});
