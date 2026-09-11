#!/bin/zsh
# usage: wclick.sh X Y [left|middle|right|move] [OUTDIR]
# Clicks at coordinates relative to the game's content area (below the macOS title bar),
# using the geometry recorded by shot.sh. On a Retina display screenshot pixels are 2x these values.
OUT=${4:-/tmp/simutrans-test}
IFS=, read wx wy ww wh < "$OUT/geo.txt"
th=32   # macOS title bar height in points
"$(dirname "$0")/click" $((wx+$1)) $((wy+th+$2)) ${3:-left}
