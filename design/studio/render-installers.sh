#!/usr/bin/env bash
# Regenerate the native installer artwork from the catalogue's studio mark.
set -euo pipefail
cd "$(dirname "$0")"
magick -background none -density 384 mark.svg -resize 64x64 -gravity center -background '#21211f' -extent 150x57 -alpha remove -type TrueColor BMP3:header.bmp
magick -background none -density 384 mark.svg -resize 106x106 -gravity center -background '#21211f' -extent 164x314 -alpha remove -type TrueColor BMP3:welcome.bmp
magick -background none -density 384 mark.svg -resize 112x112 -gravity center -extent 150x180 PNG32:welcome.png
