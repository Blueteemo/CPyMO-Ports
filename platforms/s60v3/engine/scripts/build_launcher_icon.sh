#!/usr/bin/env bash
set -euo pipefail
logo=$(realpath -- "$1")
output=$(realpath -m -- "$2")
temp=$(mktemp -d)
trap 'rm -rf -- "$temp"' EXIT
cd "$temp"
args=()
for size in 44 32 24; do
    # Reference canvas 88 × 88: outer rect (4, 4, 80, 80), radius 18.
    # A 22x intermediate represents these ratios as whole pixels at all three sizes.
    canvas=$((size * 22))
    inset=$size
    inner=$((size * 20))
    radius=$((size * 9 / 2))
    # Approved framing from the 1254 × 1254 master. The outer mask is unchanged.
    magick "$logo" -crop 1120x1120+104+2 +repage -alpha off -strip -filter Lanczos -resize "${inner}x${inner}!" \
        -virtual-pixel edge -set option:distort:viewport "${canvas}x${canvas}+0+0" \
        -distort AffineProjection "1,0,0,1,$inset,$inset" +repage \
        -scale "${size}x${size}!" -type TrueColor "BMP3:icon-$size.bmp"

    # Average the edge coverage into an EGray256 alpha mask. Its byte values
    # go from transparent (0) to opaque (255), regardless of aInvertMask.
    python3 - "$canvas" "$inset" "$radius" > mask.pgm <<'PYMASK'
import sys

canvas, inset, radius = map(int, sys.argv[1:])
end = canvas - inset
radius_squared = (2 * radius) ** 2
mask = bytearray(canvas * canvas)
for y in range(inset, end):
    # Check the sample centers in doubled coordinates to avoid rounding.
    dy = max(0, 2 * (inset + radius - y) - 1,
             2 * (y - (end - radius)) + 1)
    first = inset
    while (2 * (inset + radius - first) - 1) ** 2 + dy ** 2 > radius_squared:
        first += 1
    mask[y * canvas + first:(y + 1) * canvas - first] = b"\xff" * (canvas - 2 * first)
sys.stdout.buffer.write(f"P5\n{canvas} {canvas}\n255\n".encode("ascii"))
sys.stdout.buffer.write(mask)
PYMASK
    magick mask.pgm -set colorspace sRGB -scale "${size}x${size}!" \
        -type TrueColor "BMP3:mask-$size.bmp"
    args+=("/c16icon-$size.bmp" "/8mask-$size.bmp")
done
bmconv icons.mbm "${args[@]}"
bmconv /v icons.mbm
cp -- icons.mbm "$output"
