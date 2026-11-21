"""Prepare product-scoped GitHub releases from the native CMake project version."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
STUDIO = json.loads((ROOT / "studio.json").read_text())
PRODUCTS = STUDIO["products"]


def product(slug):
    settings = PRODUCTS[slug]
    source = ROOT / "products" / slug
    project = (source / "CMakeLists.txt").read_text()
    version, = re.findall(r"(?im)^project\(\w+\s+VERSION\s+(\d+\.\d+\.\d+)\s", project)
    stem = settings["name"].replace(" ", "-") + "-" + version
    return dict(settings, slug=slug, version=version, tag=f"{slug}/v{version}",
                assets={"macos": stem + ".pkg", "windows": stem + "-windows-x64.exe",
                        "linux": stem + "-ubuntu-26.04-x86_64.tar.gz"})


def record(item, directory, sha):
    if not re.fullmatch(r"[a-f0-9]{40}", sha):
        raise ValueError("Expected a full source commit SHA")
    downloads = {}
    sums = []
    for os_name, name in item["assets"].items():
        data = (directory / name).read_bytes()
        if not data:
            raise ValueError(f"Empty release package: {name}")
        digest = hashlib.sha256(data).hexdigest()
        downloads[os_name] = dict(file=name, bytes=len(data), sha256=digest)
        sums.append(f"{digest}  {name}")
    metadata = dict(product=item["slug"], version=item["version"], source=sha, downloads=downloads)
    (directory / "release.json").write_text(json.dumps(metadata, indent=2) + "\n")
    (directory / "SHA256SUMS").write_text("\n".join(sums) + "\n")


def notes(item, sha):
    source = ROOT / "products" / item["slug"]
    changelog = (source / "docs/releases" / (item["version"] + ".md")).read_text(encoding="utf-8").strip()
    return (changelog + "\n\n[Source and build instructions](" + STUDIO["repository"] +
            f"/tree/{sha}/products/{item['slug']}). Verify downloads with SHA256SUMS or GitHub artifact attestations.")



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("env", "record", "notes"))
    parser.add_argument("slug", choices=PRODUCTS)
    args = parser.parse_args()
    item = product(args.slug)
    if args.command == "notes":
        print(notes(item, os.environ["GITHUB_SHA"]))
        return
    if args.command == "record":
        record(item, Path("dist"), os.environ["GITHUB_SHA"])
        return
    artifact_dir = ROOT / "products" / args.slug / "build/release" / item["artifacts"]
    values = dict(PRODUCT=args.slug, NAME=item["name"], VERSION=item["version"], TAG=item["tag"],
                  PUBLISHER=STUDIO["publisher"], WEBSITE=STUDIO["website"], BUNDLE=item["bundle"], ARTIFACTS=artifact_dir,
                  **{platform.upper() + "_FILE": name for platform, name in item["assets"].items()})
    with open(os.environ["GITHUB_ENV"], "a", encoding="utf-8") as out:
        for key, value in values.items():
            print(f"{key}={value}", file=out)


if __name__ == "__main__":
    main()
