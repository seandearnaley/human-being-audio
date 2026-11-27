#!/usr/bin/env -S uv run --quiet
# /// script
# requires-python = ">=3.11"
# dependencies = ["fonttools", "uharfbuzz", "picosvg"]
# ///
"""Generates Tract Form's identity: every logo, the app icon and the press images.

    brand/build.py                 # logos, icons, brand sheet, press images
    brand/build.py --screenshots   # also re-render the interface screenshots first

Everything is drawn here as vectors. The logotype is custom lettering made of
strokes; supporting text is converted to outlines, so no file depends on a font
being installed. Needs `resvg` (brew install resvg)
to rasterise and macOS `iconutil` for the .icns. `--screenshots` needs a built
`tf-plugin-snapshot` (cmake --build --preset dev).
"""
from __future__ import annotations

import math
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import uharfbuzz as hb
from picosvg.svg import SVG
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parent.parent
BRAND = ROOT / "brand"
PRESS = ROOT / "press"
FONTS = ROOT.parents[1] / "modules" / "visage-ui" / "fonts"
RESVG = shutil.which("resvg") or "/opt/homebrew/bin/resvg"

# The palette is the plugin's own (ui/src/theme.h).
INK = "#0a0d12"      # the window
PANEL = "#10151c"
RAISED = "#161d26"
EDGE = "#1e2732"
TEXT = "#e6ebf0"
DIM = "#8a95a3"
CYAN = "#3fd9e8"     # the master sine and the sub
AMBER = "#f5a742"    # the growl
VIOLET = "#a98bff"   # movement
# Deeper cyan and amber keep their contrast on a light background.
CYAN_DEEP = "#0e9fb0"
AMBER_DEEP = "#dc8a17"
PAPER = "#f3f5f7"

NAME = "Tract Form"
DESCRIPTOR = "Sub-locked neuro bass synthesizer"


# --- Lettering -------------------------------------------------------------

def outline(text: str, weight: str = "SemiBold", size: float = 100.0, tracking: float = 0.0) -> tuple[str, float]:
    """Text as an SVG path with its baseline at y = 0, and its advance width."""
    path = str(FONTS / f"Inter-{weight}.ttf")
    face = hb.Face(hb.Blob.from_file_path(path))
    font = hb.Font(face)
    buffer = hb.Buffer()
    buffer.add_str(text)
    buffer.guess_segment_properties()
    hb.shape(font, buffer, {"kern": True, "liga": True})
    ttfont = TTFont(path)
    glyphs = ttfont.getGlyphSet()
    scale = size / face.upem
    x = 0.0
    commands = []
    for info, position in zip(buffer.glyph_infos, buffer.glyph_positions):
        pen = SVGPathPen(glyphs)
        transform = (scale, 0, 0, -scale, (x + position.x_offset) * scale, 0)
        glyphs[ttfont.getGlyphName(info.codepoint)].draw(TransformPen(pen, transform))
        commands.append(pen.getCommands())
        x += position.x_advance + tracking * face.upem
    return " ".join(commands), (x - tracking * face.upem) * scale


# --- The mark and the logotype ----------------------------------------------
# Everything is one stroke system: a round-capped line 46 units thick on a grid
# where the x-height is 164 and ascenders reach 284. Coordinates are centre
# lines with the baseline at y = 0 and up negative.
#
# The mark is a "tf" in which the f is the t turned upside down, joined by one
# bar: the two reese voices as inversions of each other, tied together by the
# master. The logotype spells the name in the same strokes, and the t that ends
# "tract" and the f that begins "form" are that mark, bridging the two words.

STROKE = 46.0
X_HEIGHT = 164.0
ASCENT = 284.0
BOWL = X_HEIGHT / 2          # radius of o, c, a
ARCH = 66.0                  # radius of the arches in m
INK_TOP = -ASCENT - STROKE / 2
INK_BOTTOM = 4 + STROKE / 2  # the f sits 4 units low, as in the mark
INK_HEIGHT = INK_BOTTOM - INK_TOP


def stroke(d: str, colour: str) -> str:
    return (f'<path d="{d}" fill="none" stroke="{colour}" stroke-width="{STROKE:g}" '
            f'stroke-linecap="round" stroke-linejoin="round"/>')


def glyph_t(x: float) -> list[str]:
    """Stem at x, hooking right along the baseline."""
    return [f"M{x:g},{-ASCENT:g} V-64 Q{x:g},0 {x + 64:g},0 H{x + 80:g}"]


def glyph_f(x: float) -> list[str]:
    """Stem at x, hooking right at the ascender: the t turned upside down."""
    return [f"M{x:g},4 V{-ASCENT + 68:g} Q{x:g},{-ASCENT + 4:g} {x + 64:g},{-ASCENT + 4:g} H{x + 80:g}"]


def bar(x0: float, x1: float) -> str:
    return f"M{x0:g},{-X_HEIGHT:g} H{x1:g}"


# Each letter draws from its left edge and reports its width.
LETTERS = {
    "t": (lambda x: glyph_t(x + 64) + [bar(x, x + 144)], 144.0),
    "r": (lambda x: [f"M{x:g},{-X_HEIGHT:g} V0",
                     f"M{x:g},{-BOWL:g} A{BOWL:g},{BOWL:g} 0 0 1 {x + BOWL:g},{-X_HEIGHT:g} H{x + 100:g}"], 100.0),
    "a": (lambda x: [f"M{x:g},{-BOWL:g} a{BOWL:g},{BOWL:g} 0 1 0 {X_HEIGHT:g},0 a{BOWL:g},{BOWL:g} 0 1 0 {-X_HEIGHT:g},0",
                     f"M{x + X_HEIGHT:g},{-X_HEIGHT:g} V0"], X_HEIGHT),
    "c": (lambda x: [f"M{x + 134.7:g},-19.2 A{BOWL:g},{BOWL:g} 0 1 1 {x + 134.7:g},-144.8"], 136.0),
    "o": (lambda x: [f"M{x:g},{-BOWL:g} a{BOWL:g},{BOWL:g} 0 1 0 {X_HEIGHT:g},0 a{BOWL:g},{BOWL:g} 0 1 0 {-X_HEIGHT:g},0"], X_HEIGHT),
    "m": (lambda x: [f"M{x:g},{-X_HEIGHT:g} V0",
                     f"M{x:g},{-X_HEIGHT + ARCH:g} A{ARCH:g},{ARCH:g} 0 0 1 {x + 2 * ARCH:g},{-X_HEIGHT + ARCH:g} V0",
                     f"M{x + 2 * ARCH:g},{-X_HEIGHT + ARCH:g} A{ARCH:g},{ARCH:g} 0 0 1 {x + 4 * ARCH:g},{-X_HEIGHT + ARCH:g} V0"], 4 * ARCH),
}

GAP = 82.0            # between letters, centre line to centre line
MARK_STEMS = 132.0    # between the t and the f of the mark
MARK_OVERHANG = 78.0  # how far the bar runs past each stem
# Optical corrections to the gap after a letter: an r leaves air under its arm,
# and round letters sit closer than straight ones.
AFTER = {"r": -24.0, "c": -8.0, "a": -4.0, "o": -8.0}

SCHEMES = {
    # name: (t, f, bar, lettering)
    "on-dark": (CYAN, AMBER, TEXT, TEXT),
    "on-light": (CYAN_DEEP, AMBER_DEEP, INK, INK),
    "mono-white": ("#ffffff", "#ffffff", "#ffffff", "#ffffff"),
    "mono-black": ("#000000", "#000000", "#000000", "#000000"),
}


def mark_strokes(scheme: str, t_stem: float) -> str:
    t, f, bar_colour, _ = SCHEMES[scheme]
    f_stem = t_stem + MARK_STEMS
    return (stroke(glyph_t(t_stem)[0], t) + stroke(glyph_f(f_stem)[0], f)
            + stroke(bar(t_stem - MARK_OVERHANG, f_stem + MARK_OVERHANG), bar_colour))


MARK_WIDTH = MARK_STEMS + 2 * MARK_OVERHANG + STROKE  # ink width of the mark alone


def mark_group(scheme: str) -> tuple[str, float]:
    """The mark with its ink's top-left at the origin; returns it and its ink width."""
    body = mark_strokes(scheme, MARK_OVERHANG)
    return f'<g transform="translate({STROKE / 2:g},{-INK_TOP:g})">{body}</g>', MARK_WIDTH


def logotype_group(scheme: str) -> tuple[str, float]:
    """The name with the mark inside it, ink top-left at the origin; returns it and its ink width."""
    colour = SCHEMES[scheme][3]
    parts: list[str] = []
    x = 0.0
    for letter in "trac":
        draw, width = LETTERS[letter]
        parts += [stroke(d, colour) for d in draw(x)]
        x += width + GAP + AFTER.get(letter, 0.0)
    t_stem = x + MARK_OVERHANG
    parts.append(mark_strokes(scheme, t_stem))
    x = t_stem + MARK_STEMS + MARK_OVERHANG + GAP - 4
    for letter in "orm":
        draw, width = LETTERS[letter]
        parts += [stroke(d, colour) for d in draw(x)]
        x += width + GAP + AFTER.get(letter, 0.0)
    ink_width = x - GAP + STROKE
    return f'<g transform="translate({STROKE / 2:g},{-INK_TOP:g})">{"".join(parts)}</g>', ink_width


def svg(width: float, height: float, body: str, defs: str = "") -> str:
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" '
        f'width="{width:g}" height="{height:g}" viewBox="0 0 {width:g} {height:g}">'
        f'{"<defs>" + defs + "</defs>" if defs else ""}{body}</svg>\n'
    )


def placed(group_and_width: tuple[str, float], x: float, y: float, height: float) -> tuple[str, float]:
    """Scales a mark or logotype to `height` with its ink's top-left at (x, y)."""
    group, width = group_and_width
    scale = height / INK_HEIGHT
    return f'<g transform="translate({x:.3f},{y:.3f}) scale({scale:.5f})">{group}</g>', width * scale


def placed_mark(scheme: str, x: float, y: float, height: float) -> tuple[str, float]:
    return placed(mark_group(scheme), x, y, height)


def placed_logotype(scheme: str, x: float, y: float, height: float) -> tuple[str, float]:
    return placed(logotype_group(scheme), x, y, height)


def mark_svg(scheme: str) -> str:
    group, width = mark_group(scheme)
    return svg(width, INK_HEIGHT, group)


def logotype_svg(scheme: str) -> str:
    group, width = logotype_group(scheme)
    return svg(width, INK_HEIGHT, group)


# --- The app icon ----------------------------------------------------------

TILE_GRADIENT = (
    f'<linearGradient id="tile" x1="0" y1="0" x2="0" y2="1">'
    f'<stop offset="0" stop-color="{RAISED}"/><stop offset="1" stop-color="{INK}"/></linearGradient>'
)


def tile(size: float, x: float = 0.0, y: float = 0.0) -> str:
    """The rounded-square icon body with the mark on it, `size` across."""
    radius = size * 0.2237  # the macOS icon corner
    edge = max(size / 256, 0.75)
    mark_height = size * 0.652
    group, width = placed_mark("on-dark", x + (size - mark_height * MARK_WIDTH / INK_HEIGHT) / 2, y + (size - mark_height) / 2, mark_height)
    return (
        f'<rect x="{x}" y="{y}" width="{size}" height="{size}" rx="{radius}" fill="url(#tile)"/>'
        f'<rect x="{x + edge / 2}" y="{y + edge / 2}" width="{size - edge}" height="{size - edge}" '
        f'rx="{radius - edge / 2}" fill="none" stroke="{EDGE}" stroke-width="{edge}"/>{group}'
    )


def icon_svg(canvas: float = 1024.0, body: float = 824.0) -> str:
    """A macOS app icon: the body sits inside the canvas with Apple's margin."""
    offset = (canvas - body) / 2
    return svg(canvas, canvas, tile(body, offset, offset), TILE_GRADIENT)


def tile_svg(size: float = 512.0) -> str:
    """The icon body edge to edge, for avatars and favicons."""
    return svg(size, size, tile(size), TILE_GRADIENT)


# --- Output ----------------------------------------------------------------

def write(path: Path, text: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    return path


def rasterise(source: Path, target: Path, width: int | None = None, height: int | None = None) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    command = [RESVG, str(source), str(target)]
    if width:
        command += ["--width", str(width)]
    if height:
        command += ["--height", str(height)]
    subprocess.run(command, check=True)


def build_logos() -> None:
    """Each logo three ways: stroked SVG (editable), filled outlines (for print), and PNG."""
    logo = BRAND / "logo"
    for scheme in SCHEMES:
        for name, text, width in ((f"tract-form-logotype-{scheme}", logotype_svg(scheme), 2400),
                                  (f"tract-form-mark-{scheme}", mark_svg(scheme), 1024)):
            source = write(logo / f"{name}.svg", text)
            write(logo / "outlined" / f"{name}.svg", SVG.parse(source).topicosvg().tostring(pretty_print=True) + "\n")
            rasterise(source, logo / "png" / f"{name}.png", width=width)


def build_icons() -> None:
    icon = BRAND / "icon"
    app = write(icon / "tract-form-icon.svg", icon_svg())
    square = write(icon / "tract-form-tile.svg", tile_svg())
    for size in (16, 32, 64, 128, 256, 512, 1024):
        rasterise(app, icon / "png" / f"icon-{size}.png", width=size)
    rasterise(square, icon / "png" / "tile-512.png", width=512)

    with tempfile.TemporaryDirectory() as scratch:
        iconset = Path(scratch) / "TractForm.iconset"
        iconset.mkdir()
        for points in (16, 32, 128, 256, 512):
            rasterise(app, iconset / f"icon_{points}x{points}.png", width=points)
            rasterise(app, iconset / f"icon_{points}x{points}@2x.png", width=points * 2)
        subprocess.run(["iconutil", "-c", "icns", str(iconset), "-o", str(icon / "TractForm.icns")], check=True)


def label(text: str, x: float, y: float, size: float = 26.0, colour: str = DIM, weight: str = "Medium") -> str:
    path, _ = outline(text, weight, size)
    return f'<path transform="translate({x},{y})" d="{path}" fill="{colour}"/>'


def build_brand_sheet() -> None:
    """One page: the logotype, its variants, clear space, colours and type."""
    width, height = 2400, 1760
    body = [f'<rect width="{width}" height="{height}" fill="{INK}"/>']

    group, _ = placed_logotype("on-dark", 140, 130, 250)
    body.append(group)
    body.append(label(DESCRIPTOR, 142, 470, 34, DIM, "Regular"))

    body.append(label("Logotype", 140, 620))
    cards = [("on-dark", INK, EDGE), ("on-light", PAPER, PAPER), ("mono-white", "#1b222c", "#1b222c"), ("mono-black", "#ffffff", "#ffffff")]
    for i, (scheme, fill, edge) in enumerate(cards):
        x = 140 + i * 540
        body.append(f'<rect x="{x}" y="650" width="500" height="230" rx="20" fill="{fill}" stroke="{edge}" stroke-width="2"/>')
        group, logo_width = placed_logotype(scheme, 0, 0, 70)
        body.append(f'<g transform="translate({x + (500 - logo_width) / 2:.2f},{650 + 80})">{group}</g>')

    body.append(label("Mark, icon and clear space", 140, 990))
    group, mark_width = placed_mark("on-dark", 140, 1030, 230)
    body.append(group)
    body.append(f'<g transform="translate(470,1015)">{tile(260)}</g>')
    clear_x, clear_y, clear_height = 900.0, 1080.0, 130.0
    group, logo_width = placed_logotype("on-dark", clear_x, clear_y, clear_height)
    pad = clear_height * X_HEIGHT / INK_HEIGHT  # one x-height on every side
    body.append(f'<rect x="{clear_x - pad}" y="{clear_y - pad}" width="{logo_width + 2 * pad}" height="{clear_height + 2 * pad}" '
                f'rx="6" fill="none" stroke="{VIOLET}" stroke-width="2" stroke-dasharray="8 8" opacity="0.7"/>')
    body.append(group)

    body.append(label("Colour", 140, 1400))
    swatches = [("Window", INK), ("Panel", PANEL), ("Text", TEXT), ("Sub", CYAN), ("Growl", AMBER), ("Movement", VIOLET)]
    for i, (name, colour) in enumerate(swatches):
        x = 140 + i * 250
        body.append(f'<rect x="{x}" y="1430" width="220" height="120" rx="16" fill="{colour}" stroke="{EDGE}" stroke-width="2"/>')
        body.append(label(name, x, 1595, 26, TEXT))
        body.append(label(colour.upper(), x, 1632, 22, DIM, "Regular"))

    body.append(label("Supporting type", 1700, 1400))
    body.append(label("Inter", 1700, 1500, 84, TEXT, "SemiBold"))
    body.append(label("SemiBold, Medium, Regular", 1700, 1558, 28, DIM, "Regular"))
    body.append(label("SIL Open Font License", 1700, 1598, 22, DIM, "Regular"))

    rasterise(write(BRAND / "brand-sheet.svg", svg(width, height, "".join(body), TILE_GRADIENT)), BRAND / "brand-sheet.png")


def screenshot_href(name: str, relative_to: Path) -> str | None:
    shot = PRESS / "screenshots" / name
    return str(Path("..") / "screenshots" / name) if shot.exists() and relative_to == PRESS / "images" else None


def backdrop(width: float, height: float) -> tuple[str, str]:
    defs = (
        f'<radialGradient id="glowc" cx="0.18" cy="0.05" r="0.9"><stop offset="0" stop-color="{CYAN}" stop-opacity="0.16"/>'
        f'<stop offset="0.55" stop-color="{CYAN}" stop-opacity="0"/></radialGradient>'
        f'<radialGradient id="glowa" cx="0.95" cy="0.9" r="0.9"><stop offset="0" stop-color="{AMBER}" stop-opacity="0.13"/>'
        f'<stop offset="0.55" stop-color="{AMBER}" stop-opacity="0"/></radialGradient>'
    )
    body = (f'<rect width="{width}" height="{height}" fill="{INK}"/>'
            f'<rect width="{width}" height="{height}" fill="url(#glowc)"/><rect width="{width}" height="{height}" fill="url(#glowa)"/>')
    return defs, body


def build_press_images() -> None:
    images = PRESS / "images"

    # Hero: the logotype above the interface.
    width, height = 2400, 1350
    defs, body = backdrop(width, height)
    group, _ = placed_logotype("on-dark", 150, 120, 170)
    body += group + label(DESCRIPTOR, 152, 372, 40, DIM, "Regular")
    href = screenshot_href("tract-form-interface.png", images)
    if href:
        shot_width = 2100
        shot_height = shot_width * 9 / 16
        x, y = (width - shot_width) / 2, 470
        defs += (f'<clipPath id="shot"><rect x="{x}" y="{y}" width="{shot_width}" height="{shot_height}" rx="28"/></clipPath>'
                 f'<filter id="lift" x="-10%" y="-10%" width="120%" height="130%"><feGaussianBlur stdDeviation="40"/></filter>')
        body += (f'<rect x="{x + 20}" y="{y + 50}" width="{shot_width - 40}" height="{shot_height}" rx="28" fill="#000" opacity="0.55" filter="url(#lift)"/>'
                 f'<image href="{href}" xlink:href="{href}" x="{x}" y="{y}" width="{shot_width}" height="{shot_height}" clip-path="url(#shot)" preserveAspectRatio="xMidYMid slice"/>'
                 f'<rect x="{x}" y="{y}" width="{shot_width}" height="{shot_height}" rx="28" fill="none" stroke="{EDGE}" stroke-width="3"/>')
    rasterise(write(images / "tract-form-hero.svg", svg(width, height, body, defs)), images / "tract-form-hero.png")

    # Social card (also GitHub's social preview size).
    width, height = 1280, 640
    defs, body = backdrop(width, height)
    group, logo_width = placed_logotype("on-dark", 0, 0, 150)
    body += f'<g transform="translate({(width - logo_width) / 2:.2f},205)">{group}</g>'
    path, text_width = outline(DESCRIPTOR, "Regular", 34)
    body += f'<path transform="translate({(width - text_width) / 2:.2f},446)" d="{path}" fill="{DIM}"/>'
    rasterise(write(images / "tract-form-social.svg", svg(width, height, body, defs)), images / "tract-form-social.png")

    # Square, for avatars and posts.
    size = 1080
    defs, body = backdrop(size, size)
    group, mark_width = placed_mark("on-dark", 0, 0, 400)
    body += f'<g transform="translate({(size - mark_width) / 2:.2f},{(size - 400) / 2:.2f})">{group}</g>'
    rasterise(write(images / "tract-form-square.svg", svg(size, size, body, defs)), images / "tract-form-square.png")


SCREENSHOTS = [
    # file, preset, seconds of audio before the picture
    ("tract-form-interface.png", "Coupled Reese", 1.9),
    ("tract-form-neuro.png", "Chrome Rasp", 2.3),
    ("tract-form-talk.png", "Speaking Pipe", 1.6),
    ("tract-form-reese.png", "Twin Filament", 2.1),
]


def build_screenshots() -> None:
    tool = ROOT / "build" / "dev" / "plugin" / "tf-plugin-snapshot"
    if not tool.exists():
        sys.exit(f"{tool} is not built: run cmake --build --preset dev first")
    for name, preset, seconds in SCREENSHOTS:
        target = PRESS / "screenshots" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run([str(tool), str(target), "2560", "1440", "29", str(seconds), preset], check=True, stdout=subprocess.DEVNULL)
        # The tool writes uncompressed pixels (about 15 MB); re-encode in place.
        subprocess.run(["sips", "-s", "format", "png", str(target), "--out", str(target)], check=True, stdout=subprocess.DEVNULL)


def main() -> None:
    if "--screenshots" in sys.argv:
        build_screenshots()
    build_logos()
    build_icons()
    build_brand_sheet()
    build_press_images()
    print("brand and press assets written")


if __name__ == "__main__":
    main()
