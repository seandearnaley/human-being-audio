import { execFileSync } from "node:child_process";
import { createHash } from "node:crypto";
import { createReadStream } from "node:fs";
import { readFile, stat } from "node:fs/promises";
import { basename, dirname } from "node:path";
import { pipeline } from "node:stream/promises";
import { expect, test } from "@playwright/test";
import studio from "../../studio.json" with { type: "json" };

const platforms = [
  ["macos", "macOS"],
  ["windows", "Windows x64"],
  ["linux", "Ubuntu 26.04 x86_64"],
] as const;
const tag = process.env.RELEASE_TAG ?? "";
const repo = new URL(studio.repository).pathname.slice(1);
let record: {
  product: keyof typeof studio.products;
  version: string;
  downloads: Record<typeof platforms[number][0], { file: string; bytes: number; sha256: string }>;
};

function ghRelease(...args: string[]) {
  return execFileSync("gh", ["release", ...args, "--repo", repo], { encoding: "utf8" });
}

test.beforeAll(async ({}, info) => {
  const match = /^([a-z0-9][a-z0-9-]*)\/v(\d+\.\d+\.\d+)$/.exec(tag);
  if (!match || !Object.hasOwn(studio.products, match[1])) {
    throw new Error("Expected a studio product release tag, for example pole-field/v0.1.0");
  }
  const [, product, version] = match;
  const release = JSON.parse(ghRelease("view", tag, "--json", "isImmutable,targetCommitish"));
  expect(release.isImmutable, `GitHub release ${tag} must be immutable`).toBe(true);

  const manifest = info.outputPath("release.json");
  ghRelease("download", tag, "--pattern", "release.json", "--dir", dirname(manifest));
  ghRelease("verify-asset", tag, manifest);
  record = JSON.parse(await readFile(manifest, "utf8"));
  expect(record).toMatchObject({ product, version, source: release.targetCommitish });
  expect(Object.keys(record.downloads).sort()).toEqual(platforms.map(([os]) => os).sort());
});

for (const [os, label] of platforms) {
  test(`live ${label} button delivers the immutable release asset`, async ({ page }, info) => {
    test.setTimeout(5 * 60_000);
    const { product, version, downloads } = record;
    const site = new URL(`/#${product}`, studio.website).href;
    const card = page.locator(`article[data-plugin="${product}"]`);
    const heading = card.getByRole("heading", {
      name: `Download ${studio.products[product].name} ${version}`, exact: true,
    });
    // The public page and release feed each revalidate every 60 seconds.
    await expect.poll(async () => {
      const response = page.url() === site
        ? await page.reload({ waitUntil: "domcontentloaded" })
        : await page.goto(site, { waitUntil: "domcontentloaded" });
      expect(response?.status(), site).toBe(200);
      return heading.isVisible();
    }, { message: `Waiting for ${tag} on ${site}`, intervals: [10_000], timeout: 180_000 }).toBe(true);

    const { file, bytes, sha256 } = downloads[os];
    expect(basename(file)).toBe(file);
    const button = card.getByRole("link", { name: label, exact: true });
    await expect(button).toHaveCount(1);
    const href = await button.getAttribute("href");
    expect(decodeURIComponent(href ?? "")).toBe(`${studio.repository}/releases/download/${tag}/${file}`);

    const [download] = await Promise.all([page.waitForEvent("download", { timeout: 30_000 }), button.click()]);
    expect(download.suggestedFilename()).toBe(file);
    const saved = info.outputPath(file);
    await download.saveAs(saved);
    expect((await stat(saved)).size).toBe(bytes);
    const digest = createHash("sha256");
    await pipeline(createReadStream(saved), digest);
    expect(digest.digest("hex")).toBe(sha256);
    ghRelease("verify-asset", tag, saved);
    console.log(`${label}: ${file} (${bytes} bytes, verified SHA-256 ${sha256})`);
  });
}
