#!/bin/zsh
set -eu
base="$(cd "$(dirname "$0")" && pwd)"
dep_input="${1:-$base/build/dependencies/prefix}"
out="${2:-$base/dist}"
[[ "$(uname -sm)" == "Darwin arm64" ]] || { echo "Requires an Apple Silicon Mac." >&2; exit 1; }
xcrun --sdk macosx --show-sdk-version
[[ -d "$dep_input" ]] || { echo "Build dependencies first: ./build-deps.sh" >&2; exit 1; }
prefix="$(cd "$dep_input" && pwd)"
for lib in SDL2 avformat avcodec swresample avutil; do
    [[ -f "$prefix/lib/lib$lib.a" ]] || { echo "Missing static $lib in $prefix." >&2; exit 1; }
done
mkdir -p "$out"; out="$(cd "$out" && pwd)"
[[ ! -e "$out/CPyMO.app" ]] || { echo "Refusing to overwrite an existing CPyMO.app in $out." >&2; exit 1; }
stage="$(mktemp -d "$out/.stage.XXXXXX")"
trap 'rm -rf "$stage"' EXIT
app="$stage/CPyMO.app"; res="$app/Contents/Resources"
mkdir -p "$res" "$app/Contents/MacOS"
cp "$base/app/Info.plist" "$app/Contents/"
cp "$base/assets/"* "$res/"
"$base/source/build-engine.sh" "$prefix" "$res/CPyMO"
xcrun swiftc -module-cache-path "$stage/module-cache" -parse-as-library \
    "$base/app/CPyMOLibrary.swift" -o "$app/Contents/MacOS/CPyMOLibrary"
codesign --force --sign - "$res/CPyMO"
codesign --force --sign - "$app"
codesign --verify --deep --strict "$app"
"$app/Contents/MacOS/CPyMOLibrary" --test-selection
"$app/Contents/MacOS/CPyMOLibrary" --test-grid
mv "$app" "$out/CPyMO.app"
printf 'Built %s\n' "$out/CPyMO.app"
