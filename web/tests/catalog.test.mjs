import assert from "node:assert/strict";
import { test } from "node:test";
import { readCatalog } from "../lib/releases.mjs";
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
    [],
  );
});
test("each product resolves to its highest public numeric version across pages", async () => {
  const a = fixture(),
    b = fixture("livecut-redux", "1.0.10"),
    other = fixture("tract-form", "0.2.0");
  const rows = Array.from({ length: 100 }, () => a.row);
  const result = await readCatalog(async (url) =>
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
    await assert.rejects(() =>
      readCatalog(async (url) =>
        url.includes("api.github.com") ? [f.row] : f.record,
      ),
    );
  }
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
  const [release] = await readCatalog();
  assert.equal(release.downloads.length, 3);
});
