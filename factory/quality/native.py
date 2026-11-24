"""Use the studio catalogue and the actual CMake compilation databases for native tools."""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SHARED = {".clang-format", ".clang-format-ignore", ".clang-tidy", ".clangd", "studio.json",
          ".github/workflows/native.yml", ".gitmodules", "factory/release/standalone-linux.py"}
VENDOR = {"third_party", "libs", "build", "_deps", ".cpm-cache"}
TRANSLATIONS = {".c", ".cc", ".cpp", ".cxx", ".m", ".mm"}


def affected(products, changes):
    if changes is None or any(p in SHARED or p.startswith(("cmake/", "modules/", "factory/quality/")) for p in changes):
        return products
    return [name for name in products if any(
        p.startswith(f"products/{name}/") and not p.endswith(".md") and not p.startswith(f"products/{name}/press/")
        for p in changes)]


def plan_checks(products, changes):
    paths = set(changes or [])
    full = changes is None or ".github/workflows/native.yml" in paths or any(p.startswith("factory/quality/") for p in paths)
    native = affected(products, changes)
    extra = {
        "livecut-redux": "livecut",
        "tract-form": "tract-form",
        "pole-field": "pole-field",
        "cutline": "cutline",
        "torus-glide": "torus-glide",
    }
    windows = [p for p in native if p != "livecut-redux"]
    jobs = ["ubuntu", "format", "realtime"] if native else []
    if not native and any(p.startswith("research/") and Path(p).suffix in
            TRANSLATIONS | {".h", ".hh", ".hpp", ".hxx", ".inl"} for p in paths):
        jobs.append("format")
    if windows:
        jobs.append("windows")
    for name, workflow in extra.items():
        if name in native or f".github/workflows/{workflow}.yml" in paths:
            jobs.append(workflow)
    if full or "studio.json" in paths or any(p.startswith(("web/", "design/studio/")) or
            "/press/screenshots/" in p or p in {".github/workflows/website.yml", "products/livecut-redux/docs/editor.png"} for p in paths):
        jobs.append("website")
    if full or "studio.json" in paths or any(p.startswith(("factory/", "design/studio/")) or "/docs/releases/" in p or
            (p.startswith("products/") and len(Path(p).parts) == 3 and p.endswith("/CMakeLists.txt")) or
            p in {".github/workflows/factory.yml", ".github/workflows/release.yml"} for p in paths):
        jobs.append("factory")
    return {"products": native, "windows": windows, "jobs": jobs}


def lint_inputs(product, sources, changes):
    if changes is None:
        return sources
    relevant = [p for p in changes if affected([product], [p])]
    if any(Path(p).suffix in {".h", ".hh", ".hpp", ".hxx", ".inl", ".inc", ".in", ".cmake", ".json"}
           or Path(p).name == "CMakeLists.txt" or p in SHARED or p.startswith(("cmake/", "factory/quality/"))
           or VENDOR.intersection(Path(p).parts) for p in relevant):
        return sources
    return sources.intersection(relevant)


def changed_files(base):
    if not base or set(base) == {"0"}:
        return None
    output = subprocess.check_output(["git", "diff", "--no-renames", "--name-only", "-z", base, "HEAD"], cwd=ROOT)
    return [os.fsdecode(p) for p in output.split(b"\0") if p]


def studio_commands(root, product, build):
    root = root.resolve()
    for command in json.loads((build / "compile_commands.json").read_text()):
        absolute = os.path.abspath(os.path.join(command["directory"], command["file"]))
        path = Path(absolute).resolve()
        if not path.is_relative_to(root):
            continue
        relative = path.relative_to(root)
        if (relative.parts[:2] == ("products", product) or relative.parts[:1] == ("modules",)) and (
                path.suffix in TRANSLATIONS and not VENDOR.intersection(relative.parts)):
            yield relative.as_posix(), absolute, command


def editor_commands(root, products):
    commands = {}
    for product in products:
        build = root / "products" / product / "build/dev"
        for path, _, command in studio_commands(root, product, build):
            commands.setdefault(path, command)
    if not commands:
        raise ValueError("No studio source files in the selected compilation databases")
    return list(commands.values())


def lint(product, build, changes, runner, tidy):
    paths = {}
    for path, absolute, _ in studio_commands(ROOT, product, build):
        paths.setdefault(path, set()).add(absolute)
    if not paths:
        raise SystemExit(f"No studio translation units in {build} for {product}")
    selected = sorted(lint_inputs(product, set(paths), changes))
    print(f"{product}: checking {len(selected)} of {len(paths)} studio translation units", flush=True)
    if selected:
        pattern = "^(?:" + "|".join(re.escape(alias) for path in selected for alias in sorted(paths[path])) + ")$"
        subprocess.run([sys.executable, runner, "-clang-tidy-binary", tidy, "-p", str(build), "-quiet", "-source-filter", pattern], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest="action", required=True)
    plan = actions.add_parser("plan")
    plan.add_argument("--base")
    check = actions.add_parser("lint")
    check.add_argument("product")
    check.add_argument("build", type=Path)
    check.add_argument("--base", default=os.environ.get("HBA_QUALITY_BASE"))
    check.add_argument("--runner", required=True)
    check.add_argument("--tidy", required=True)
    editor = actions.add_parser("editor", help="index configured dev builds for clangd")
    editor.add_argument("products", nargs="*")
    args = parser.parse_args()
    products = list(json.loads((ROOT / "studio.json").read_text())["products"])
    if args.action == "plan":
        for name, value in plan_checks(products, changed_files(args.base)).items():
            print(f"{name}={json.dumps(value, separators=(',', ':'))}")
    elif args.action == "lint":
        lint(args.product, args.build, changed_files(args.base), args.runner, args.tidy)
    else:
        selected = args.products or products
        if set(selected) - set(products):
            parser.error("unknown studio product")
        try:
            commands = editor_commands(ROOT, selected)
        except FileNotFoundError as e:
            parser.exit(1, f"Missing {e.filename}. Configure that product with cmake --preset dev.\n")
        with tempfile.NamedTemporaryFile(mode="w", dir=ROOT, delete=False) as output:
            json.dump(commands, output)
        os.replace(output.name, ROOT / "compile_commands.json")
        print(f"Indexed {len(commands)} compile commands for {', '.join(selected)}.")


if __name__ == "__main__":
    main()
