#!/usr/bin/env bash
set -euo pipefail
if (( $# != 1 )); then
    printf 'Usage: %s UPSTREAM_WENQUANYI_16PX_TTF\n' "$0" >&2
    exit 2
fi
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
assets="$root/assets/fonts"
pyftsubset "$1" --output-file="$assets/story-pixel.ttf" \
    --unicodes-file="$assets/story-codepoints.txt" --name-IDs='*' --name-languages='*' --no-hinting
