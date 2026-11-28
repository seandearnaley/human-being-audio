import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { test } from "node:test";
import { FIELD_GUIDE_CHAPTERS, PRODUCTS, PRODUCT_SLUGS, SCULPTURE_FORMS, STUDIO } from "../lib/catalog.mjs";
import { readCatalog } from "../lib/releases.mjs";
import audioManifest from "../app/audioManifest.json" with { type: "json" };

test("catalog SSOT stays in strict parity with studio.json, sculpture forms, field guides, and audioManifest", () => {
  assert.deepEqual(PRODUCT_SLUGS, Object.keys(STUDIO.products));
  assert.equal(PRODUCTS.length, PRODUCT_SLUGS.length);
  const validForms = new Set(SCULPTURE_FORMS.map((f) => f.id));
  for (const product of PRODUCTS) {
    assert.ok(product.name && product.kind && product.body && product.alt && product.credits);
    assert.ok(Number.isFinite(product.hue) && product.hue >= 0 && product.hue < 360);
    assert.ok(validForms.has(product.form), `Unknown form ${product.form} for ${product.slug}`);
    assert.ok(Array.isArray(audioManifest[product.slug]) && audioManifest[product.slug].length >= 5);
    assert.ok(
      FIELD_GUIDE_CHAPTERS.some((ch) => ch.slug === product.slug && ch.hue === product.hue),
      `Missing or hue-drifted field guide chapter for ${product.slug}`,
    );
  }
});
const repo = "https://github.com/seandearnaley/human-being-audio";
const sha = "a".repeat(40),
  digest = "b".repeat(64);
function fixture(slug = "livecut-redux", version = "1.0.1") {
  const stem =
    (slug === "tract-form" ? "Tract-Form" : "Livecut-Redux") + "-" + version;
  const tag = slug + "/v" + version;
  const prefix = repo + "/releases/download/" + encodeURIComponent(tag) + "/";
  const files = {
    macos: stem + ".pkg",
    windows: stem + "-windows-x64.exe",
    linux: stem + "-ubuntu-26.04-x86_64.tar.gz",
  };
  const row = {
    tag_name: tag,
    published_at: "2026-11-21T12:00:00Z",
    target_commitish: sha,
    html_url: repo + "/releases/tag/" + tag,
    assets: [...Object.values(files), "release.json"].map((name) => ({
      name,
      size: 4,
      digest: "sha256:" + digest,
      browser_download_url: prefix + name,
    })),
  };
  const record = {
    product: slug,
    version,
    source: sha,
    downloads: Object.fromEntries(
      Object.entries(files).map(([os, file]) => [
        os,
        { file, bytes: 4, sha256: digest },
      ]),
    ),
  };
  return { row, record };
}
test("empty studio, drafts, previews and archive tags supply no download", async () => {
  const { row } = fixture();
  assert.deepEqual(
    await readCatalog(async () => [
      { ...row, draft: true },
      { ...row, prerelease: true },
      { ...row, tag_name: "archive/livecut-redux/tags/v1.0.0" },
    ]),
    { releases: [], errors: new Map() },
  );
});
test("each product resolves to its highest public numeric version across pages", async () => {
  const a = fixture(),
    b = fixture("livecut-redux", "1.0.10"),
    other = fixture("tract-form", "0.2.0");
  const rows = Array.from({ length: 100 }, () => a.row);
  const { releases: result } = await readCatalog(async (url) =>
    url.endsWith("page=1")
      ? rows
      : url.endsWith("page=2")
        ? [b.row, other.row]
        : url.includes("Tract-Form") || url.includes("tract-form")
          ? other.record
          : b.record,
  );
  assert.deepEqual(
    result.map((x) => x.version),
    ["1.0.10", "0.2.0"],
  );
  assert.equal(result[0].downloads.length, 3);
});
test("invalid asset digest, source, size or old repository URL fails explicitly", async () => {
  for (const mutate of [
    (f) => {
      f.row.assets[0].digest = "sha256:" + "0".repeat(64);
    },
    (f) => {
      f.record.source = "c".repeat(40);
    },
    (f) => {
      f.record.downloads.windows.bytes = 15;
    },
    (f) => {
      f.row.assets[0].browser_download_url =
        "https://github.com/seandearnaley/livecut/releases/download/v1/file.pkg";
    },
    (f) => {
      f.row.assets[3].browser_download_url = "https://example.com/release.json";
    },
    (f) => {
      f.row.assets.pop();
    },
  ]) {
    const f = fixture();
    mutate(f);
    const { releases, errors } = await readCatalog(async (url) =>
      url.includes("api.github.com") ? [f.row] : f.record,
    );
    assert.deepEqual(releases, []);
    assert.deepEqual([...errors.keys()], ["livecut-redux"]);
  }
});

test("release-list failures are reported for the whole catalogue", async () => {
  await assert.rejects(readCatalog(async () => { throw new Error("list fetch failed"); }), /list fetch failed/);
  await assert.rejects(readCatalog(async () => ({})), /Expected a GitHub release list/);
});

test("one invalid factory manifest withholds only that product’s three download links", async () => {
  const script = fileURLToPath(new URL("./release_manifests.py", import.meta.url));
  const records = JSON.parse(execFileSync("python3", [script], { encoding: "utf8" }));
  const manifests = new Map();
  const rows = records.map((record) => {
    const tag = record.product + "/v" + record.version;
    const base = repo + "/releases/download/" + encodeURIComponent(tag) + "/";
    manifests.set(base + "release.json", record);
    const assets = Object.values(record.downloads).map(({ file, bytes, sha256 }) => ({
      name: file, size: bytes, digest: "sha256:" + sha256, browser_download_url: base + file,
    }));
    assets.push({ name: "release.json", browser_download_url: base + "release.json" });
    return { tag_name: tag, target_commitish: record.source, assets };
  });
  const broken = records.find((record) => record.product === "cutline");
  broken.downloads.macos.sha256 = "0".repeat(64);
  const { releases, errors } = await readCatalog(async (url) =>
    url.startsWith("https://api.github.com/") ? rows : manifests.get(url),
  );

  assert.deepEqual([...errors.keys()], ["cutline"]);
  assert.match(errors.get("cutline").message, /cutline\/v/);
  assert.deepEqual(releases.map((r) => r.product), PRODUCT_SLUGS.filter((slug) => slug !== "cutline"));
  for (const release of releases) {
    const row = rows.find((entry) => entry.tag_name === release.product + "/v" + release.version);
    assert.deepEqual(release.downloads.map((d) => d.url), row.assets.slice(0, 3).map((a) => a.browser_download_url));
    assert.deepEqual(release.downloads.map((d) => d.os), ["macos", "windows", "linux"]);
  }
  assert.equal(releases.flatMap((r) => r.downloads).length, (PRODUCT_SLUGS.length - 1) * 3);
});

test("the CI credential only accompanies release API requests", async (t) => {
  const { row, record } = fixture();
  t.mock.method(globalThis, "fetch", async (url, init) => {
    const apiRequest = new URL(url).origin === "https://api.github.com";
    assert.equal(new Headers(init.headers).get("authorization"), apiRequest ? "Bearer test-token" : null);
    return Response.json(apiRequest ? [row] : record);
  });
  const previous = process.env.STUDIO_RELEASES_TOKEN;
  process.env.STUDIO_RELEASES_TOKEN = "test-token";
  t.after(() => {
    if (previous === undefined) delete process.env.STUDIO_RELEASES_TOKEN;
    else process.env.STUDIO_RELEASES_TOKEN = previous;
  });
  const { releases: [release] } = await readCatalog();
  assert.equal(release.downloads.length, 3);
});

test("every product’s catalog credits match the factory bank shipped in products/*/presets", async () => {
  const { readdir } = await import("node:fs/promises");
  for (const product of PRODUCTS) {
    const presetDir = new URL(`../../products/${product.slug}/presets/`, import.meta.url);
    const presetCount = (await readdir(presetDir)).filter((name) => name.endsWith(".json")).length;
    assert.ok(
      product.credits.includes(`${presetCount} factory presets`) ||
        product.credits.includes(`${presetCount} factory songs`),
      `Preset count mismatch for ${product.slug}: disk has ${presetCount}, credits say "${product.credits}"`,
    );
  }
  const counts = await Promise.all(["patterns", "fills", "presets"].map(async (directory) =>
    (await readdir(new URL(`../../products/cutline/${directory}/`, import.meta.url))).filter(name => name.endsWith(".json")).length));
  const cutline = PRODUCTS.find(({ slug }) => slug === "cutline");
  const [patterns, fills, presets] = counts;
  assert.ok(cutline.credits.includes(`${patterns} patterns`));
  assert.ok(cutline.credits.includes(`${fills} fills`));
  assert.ok(cutline.credits.includes(`${presets} factory presets`));
});
