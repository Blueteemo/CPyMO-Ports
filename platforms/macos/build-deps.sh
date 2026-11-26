#!/bin/zsh
set -eu
setopt pipefail
base="$(cd "$(dirname "$0")" && pwd)"
work="${1:-$base/build/dependencies}"
mkdir -p "$work"; work="$(cd "$work" && pwd)"
prefix="$work/prefix"
mkdir -p "$work/archives" "$work/src"
cd "$work/archives"
jobs="$(/usr/sbin/sysctl -n hw.logicalcpu)"
fetch() {
    local file="$1" url="$2"
    if [[ ! -f "$file" ]]; then
        /usr/bin/curl --fail --location --retry 2 "$url" --output "$file.partial"
        mv "$file.partial" "$file"
    fi
}
fetch SDL2-2.32.10.tar.gz https://www.libsdl.org/release/SDL2-2.32.10.tar.gz
fetch ffmpeg-6.1.3.tar.xz https://ffmpeg.org/releases/ffmpeg-6.1.3.tar.xz
shasum -a 256 -c "$base/dependencies.sha256"
unpack="$(mktemp -d "$work/src/build.XXXXXX")"
tar -xzf SDL2-2.32.10.tar.gz -C "$unpack"
tar -xJf ffmpeg-6.1.3.tar.xz -C "$unpack"
(
    cd "$unpack/SDL2-2.32.10"
    ./configure --prefix="$prefix" --disable-shared --enable-static
    make -j "$jobs"; make install
)
(
    cd "$unpack/ffmpeg-6.1.3"
    ./configure --prefix="$prefix" --disable-doc --disable-programs --disable-debug \
        --disable-network --disable-autodetect --disable-encoders --disable-muxers \
        --disable-devices --disable-filters --disable-postproc --disable-swscale
    make -j "$jobs"; make install
)
printf 'Dependency prefix: %s\n' "$prefix"
