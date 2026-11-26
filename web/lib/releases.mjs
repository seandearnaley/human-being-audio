import { PRODUCT_SLUGS, PRODUCTS_BY_SLUG, STUDIO } from "./catalog.mjs";
const prefix = STUDIO.repository + "/releases/";
const api =
  "https://api.github.com/repos/" +
  new URL(STUDIO.repository).pathname.slice(1);
const suffix = {
  macos: ".pkg",
  windows: "-windows-x64.exe",
  linux: "-ubuntu-26.04-x86_64.tar.gz",
};

async function readJson(url) {
  const parsed = new URL(url);
  const isApi = parsed.origin === "https://api.github.com";
  const headers = { "User-Agent": "human-being-audio-catalogue" };
  if (isApi && process.env.STUDIO_RELEASES_TOKEN)
    headers.Authorization = "Bearer " + process.env.STUDIO_RELEASES_TOKEN;
  const result = await fetch(url, {
    next: { revalidate: isApi ? 60 : 86400 },
    signal: AbortSignal.timeout(8000),
    headers,
  });
  if (!result.ok)
    throw new Error(parsed.hostname + " release request returned " + result.status);
  return result.json();
}
function assetFor(row, name) {
  const assets = Array.isArray(row?.assets) ? row.assets : [];
  const asset = assets.find((value) => value?.name === name);
  if (
    !asset ||
    typeof asset.browser_download_url !== "string" ||
    decodeURI(asset.browser_download_url).replaceAll("%2F", "/") !==
      prefix + "download/" + row.tag_name + "/" + name
  ) {
    throw new Error(
      "Missing or misdirected asset " + name + " on " + row.tag_name,
    );
  }
  return asset;
}
async function publishedRelease({ row, slug, version }, read) {
  const record = await read(assetFor(row, "release.json").browser_download_url);
  if (
    !record ||
    record.product !== slug ||
    record.version !== version ||
    !/^[a-f0-9]{40}$/.test(record.source) ||
    record.source !== row.target_commitish
  ) {
    throw new Error("Release record and source disagree for " + row.tag_name);
  }
  const stem = PRODUCTS_BY_SLUG[slug].name.replaceAll(" ", "-") + "-" + version;
  const downloads = Object.entries(suffix).map(([os, ext]) => {
    const file = stem + ext;
    const asset = assetFor(row, file);
    const info = record.downloads?.[os];
    if (
      !info ||
      file !== info.file ||
      info.bytes <= 0 ||
      asset.size !== info.bytes ||
      !/^[a-f0-9]{64}$/.test(info.sha256) ||
      asset.digest !== "sha256:" + info.sha256
    ) {
      throw new Error("Download does not match its manifest: " + file);
    }
    return {
      os,
      file,
      url: asset.browser_download_url,
      bytes: asset.size,
      sha256: info.sha256,
    };
  });
  return {
    product: slug,
    version,
    date: typeof row.published_at === "string" ? row.published_at.slice(0, 10) : "",
    url: prefix + "tag/" + row.tag_name,
    downloads,
  };
}
export async function readCatalog(read = readJson) {
  const latest = new Map();
  for (let page = 1; page <= 20; page++) {
    const rows = await read(api + "/releases?per_page=100&page=" + page);
    if (!Array.isArray(rows)) break;
    for (const row of rows) {
      const match = /^([\w-]+)\/v(\d+\.\d+\.\d+)$/.exec(row?.tag_name ?? "");
      if (
        !match ||
        !Object.hasOwn(PRODUCTS_BY_SLUG, match[1]) ||
        row.draft ||
        row.prerelease
      )
        continue;
      const [, slug, version] = match;
      const previous = latest.get(slug);
      if (
        !previous ||
        version.localeCompare(previous.version, "en", { numeric: true }) > 0
      )
        latest.set(slug, { row, slug, version });
    }
    if (rows.length < 100) break;
  }
  const sorted = [...latest.values()].sort(
    (a, b) => PRODUCT_SLUGS.indexOf(a.slug) - PRODUCT_SLUGS.indexOf(b.slug),
  );
  return Promise.all(sorted.map((entry) => publishedRelease(entry, read)));
}
