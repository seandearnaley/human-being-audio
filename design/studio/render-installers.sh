#!/usr/bin/env bash
# Render the approved SVG once; the native editors and installer composites share its colours.
set -euo pipefail
cd "$(dirname "$0")"
resvg --width 192 mark.svg mark.png
magick mark.png -resize 46x46 -gravity center -background white -extent 150x57 -alpha remove -alpha off -type TrueColor BMP3:header.bmp
magick mark.png -resize 118x118 -gravity center -background '#12161c' -extent 164x314 -alpha remove -alpha off -type TrueColor BMP3:welcome.bmp
magick mark.png -resize 112x112 -gravity center -background none -extent 150x180 PNG32:welcome.png
