#!/usr/bin/env python3
"""Build and encode the Human Being Audio web showcase clips + waveform manifest.

Canonical developer commands:
    python3 factory/showcase/build_showcase.py [--keep-wav /path/to/dir]
    python3 factory/showcase/build_showcase.py --product strike-body
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
SHOWCASE_DIR = REPO_ROOT / "factory" / "showcase"
BUILD_DIR = SHOWCASE_DIR / "build"
WEB_AUDIO_DIR = REPO_ROOT / "web" / "public" / "audio"
WEB_MANIFEST = REPO_ROOT / "web" / "app" / "audioManifest.json"
STUDIO_CATALOG = REPO_ROOT / "studio.json"
SHOWCASE_PRODUCTS = (
    "livecut-redux",
    "tract-form",
    "pole-field",
    "cutline",
    "torus-glide",
    "strike-body",
    "prism-vein",
)
PEAK_BARS = 64


def read_wav_peaks(wav_path: Path, num_bars: int = PEAK_BARS) -> tuple[float, list[int]]:
    """Read 32-bit float stereo WAV and compute exact duration + perceptual peak bars (8..100)."""
    data = wav_path.read_bytes()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError(f"Unexpected WAV header in {wav_path}")
    # Parse chunks to locate fmt and data
    offset = 12
    sample_rate = 48000
    channels = 2
    pcm = b""
    while offset + 8 <= len(data):
        cid = data[offset : offset + 4]
        csize = struct.unpack_from("<I", data, offset + 4)[0]
        body = data[offset + 8 : offset + 8 + csize]
        if cid == b"fmt ":
            _, channels, sample_rate = struct.unpack_from("<HHI", body, 0)
        elif cid == b"data":
            pcm = body
            break
        offset += 8 + csize + (csize & 1)

    total_floats = len(pcm) // 4
    frames = total_floats // channels
    duration = frames / float(sample_rate)
    samples = struct.unpack(f"<{total_floats}f", pcm)

    raw_peaks: list[float] = []
    for b in range(num_bars):
        start_f = int(b * frames / num_bars)
        end_f = max(start_f + 1, int((b + 1) * frames / num_bars))
        # Blend RMS and peak so visual envelope shows both groove punch and sustain body
        pk = 0.0
        sq = 0.0
        cnt = 0
        stride = max(1, (end_f - start_f) // 256)
        for f in range(start_f, end_f, stride):
            l = samples[f * channels]
            r = samples[f * channels + 1] if channels > 1 else l
            a = max(abs(l), abs(r))
            if a > pk:
                pk = a
            sq += 0.5 * (l * l + r * r)
            cnt += 1
        rms = math.sqrt(sq / max(1, cnt))
        raw_peaks.append(0.55 * pk + 0.45 * min(1.0, rms * 1.9))

    max_v = max(raw_peaks) if raw_peaks else 1.0
    max_v = max(max_v, 1e-4)
    bars = [max(8, min(100, int(round(math.pow(v / max_v, 0.78) * 100)))) for v in raw_peaks]
    return round(duration, 2), bars


def run(cmd: list[str], cwd: Path | None = None) -> None:
    subprocess.run(cmd, cwd=cwd, check=True)


def encode_clip(item: dict) -> tuple[str, dict]:
    product = item["product"]
    clip_id = item["id"]
    wav_path = Path(item["wav"])
    out_dir = WEB_AUDIO_DIR / product
    out_dir.mkdir(parents=True, exist_ok=True)
    mp3_path = out_dir / f"{clip_id}.mp3"

    run(
        [
            "ffmpeg",
            "-y",
            "-v",
            "error",
            "-i",
            str(wav_path),
            "-ar",
            "48000",
            "-ac",
            "2",
            "-c:a",
            "libmp3lame",
            "-q:a",
            "2",
            "-map_metadata",
            "-1",
            str(mp3_path),
        ]
    )

    stats = subprocess.run([
        "ffmpeg", "-v", "error", "-i", str(mp3_path), "-af", "loudnorm=print_format=json:stats_file=-",
        "-f", "null", "-",
    ], capture_output=True, text=True, check=True)
    peak = float(json.loads(stats.stdout)["input_tp"])
    if not math.isfinite(peak) or peak > -1.0:
        raise ValueError(f"{mp3_path}: decoded peak {peak:.2f} dBTP exceeds the -1 dBTP delivery ceiling")

    duration, peaks = read_wav_peaks(wav_path, PEAK_BARS)
    return product, {
        "id": clip_id,
        "title": item["title"],
        "tag": item["tag"],
        "bpm": int(round(item["bpm"])),
        "key": item["key"],
        "note": item["note"],
        "duration": duration,
        "src": f"/audio/{product}/{clip_id}.mp3",
        "peaks": peaks,
    }


def main() -> int:
    catalog_products = list(json.loads(STUDIO_CATALOG.read_text())["products"])
    parser = argparse.ArgumentParser(description="Build the Human Being Audio web showcase clips.")
    parser.add_argument("--keep-wav", type=Path, default=None, help="Directory to keep rendered WAVs for ear checks")
    parser.add_argument(
        "--product",
        choices=catalog_products,
        default=None,
        help="Only re-encode and update manifest entries for one studio product",
    )
    parser.add_argument(
        "--jobs",
        type=int,
        default=max(1, min(8, os.cpu_count() or 4)),
        help="Parallel ffmpeg worker count",
    )
    args = parser.parse_args()
    if args.product and args.product not in SHOWCASE_PRODUCTS:
        supported = ", ".join(SHOWCASE_PRODUCTS)
        raise ValueError(f"No showcase recipe for {args.product} (supported: {supported})")

    cpm_cache = os.environ.get("CPM_SOURCE_CACHE", str(Path.home() / ".cache" / "CPM"))
    cmake_cmd = [
        "cmake",
        "-S",
        str(SHOWCASE_DIR),
        "-B",
        str(BUILD_DIR),
        "-DCMAKE_BUILD_TYPE=Release",
        f"-DCPM_SOURCE_CACHE={cpm_cache}",
    ]
    if shutil.which("ninja") and not (BUILD_DIR / "CMakeCache.txt").exists():
        cmake_cmd[1:1] = ["-G", "Ninja"]
    run(cmake_cmd)
    run(["cmake", "--build", str(BUILD_DIR), "--parallel"])

    tmp_ctx = None
    if args.keep_wav:
        wav_dir = args.keep_wav
        wav_dir.mkdir(parents=True, exist_ok=True)
    else:
        tmp_ctx = tempfile.TemporaryDirectory(prefix="hba-showcase-")
        wav_dir = Path(tmp_ctx.name)

    try:
        render_cmd = [str(BUILD_DIR / "studio-showcase"), str(REPO_ROOT), str(wav_dir)]
        if args.product:
            render_cmd.append(args.product)
        run(render_cmd)
        raw_items = json.loads((wav_dir / "raw-manifest.json").read_text())
        if args.product:
            raw_items = [item for item in raw_items if item["product"] == args.product]
            if not raw_items:
                raise ValueError(f"No showcase recipe for {args.product}")
        manifest = json.loads(WEB_MANIFEST.read_text()) if WEB_MANIFEST.exists() else {}
        for product in {item["product"] for item in raw_items}:
            manifest[product] = []

        # Encode clips in parallel to crisp, stripped VBR MP3 (~190-220 kbps, 48 kHz stereo)
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            encoded = list(pool.map(encode_clip, raw_items))

        for product, entry in encoded:
            manifest.setdefault(product, []).append(entry)

        ordered = {slug: manifest[slug] for slug in catalog_products if slug in manifest}
        for slug, entries in manifest.items():
            if slug not in ordered:
                ordered[slug] = entries

        WEB_MANIFEST.write_text(json.dumps(ordered, indent=2) + "\n")
        total_kb = sum(p.stat().st_size for p in WEB_AUDIO_DIR.rglob("*.mp3")) / 1024.0
        print(f"Encoded {len(raw_items)} clips ({total_kb:.0f} KB total across showcase) -> {WEB_MANIFEST}")
    finally:
        if tmp_ctx is not None:
            tmp_ctx.cleanup()
    return 0


if __name__ == "__main__":
    sys.exit(main())
