const repository = 'seandearnaley/human-being-audio';
const products = new Set(['livecut-redux', 'tract-form']);
const formats = { macos: '.pkg', windows: '-windows-x64.exe', linux: '-ubuntu-26.04-x86_64.tar.gz' };

async function readJson(url) {
  const response = await fetch(url, { headers: { 'User-Agent': 'Human-Being-Audio-catalog' } });
  if (!response.ok) throw new Error(`GitHub returned ${response.status}`);
  return response.json();
}

export function downloads(release, record) {
  if (release.tag_name !== `${record.product}/v${record.version}` || record.source !== release.target_commitish || !/^[a-f0-9]{40}$/.test(record.source)) {
    throw new Error('Release record and source tag disagree');
  }
  return Object.entries(formats).map(([os, extension]) => {
    const entry = record.downloads[os];
    const asset = release.assets.find(a => a.name === entry?.file);
    if (!asset || !asset.name.endsWith(extension) || asset.size !== entry.bytes || !/^[a-f0-9]{64}$/.test(entry.sha256) || asset.digest !== `sha256:${entry.sha256}`) {
      throw new Error(`Invalid ${os} download for ${release.tag_name}`);
    }
    return { os, file: asset.name, url: asset.browser_download_url, bytes: asset.size, sha256: entry.sha256 };
  });
}

export default async function handler(_req, res) {
  try {
    const latest = new Map();
    for (let page = 1; ; page++) {
      const rows = await readJson(`https://api.github.com/repos/${repository}/releases?per_page=100&page=${page}`);
      for (const row of rows) {
        const match = /^([\w-]+)\/v(\d+\.\d+\.\d+)$/.exec(row.tag_name);
        if (!match || !products.has(match[1]) || row.draft || row.prerelease) continue;
        const previous = latest.get(match[1]);
        if (!previous || match[2].localeCompare(previous.version, 'en', { numeric: true }) > 0) {
          latest.set(match[1], { row, version: match[2] });
        }
      }
      if (rows.length < 100) break;
    }
    const catalog = await Promise.all([...latest.values()].map(async ({ row, version }) => {
      const asset = row.assets.find(a => a.name === 'release.json');
      if (!asset) throw new Error(`Missing release record for ${row.tag_name}`);
      const record = await readJson(asset.browser_download_url);
      return { product: record.product, version, date: row.published_at.slice(0, 10),
        url: row.html_url, downloads: downloads(row, record) };
    }));
    res.setHeader('Cache-Control', 'public, max-age=60');
    res.setHeader('Vercel-CDN-Cache-Control', 'public, s-maxage=300');
    return res.status(200).json(catalog);
  } catch (error) {
    console.error(error.message);
    return res.status(502).json({ error: 'Studio release data is unavailable' });
  }
}
