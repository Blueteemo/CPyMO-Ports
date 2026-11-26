#!/bin/zsh
set -e
here="$(cd "$(dirname "$0")" && pwd)"
cd "$here/engine"
d="$1"; out="$2"; shift 2
xcrun clang -target arm64-apple-macos27.2 -O1 -g -std=gnu17 -Wno-deprecated-declarations \
 -Icpymo -I"$d/include" -DCPYMO_MAC_LIBRARY_PREVIEW -DSDL2_AUDIO_DEFAULT_SAMPLES=4096 -DENABLE_TEXT_EXTRACT -DENABLE_TEXT_EXTRACT_COPY_TO_CLIPBOARD -DENABLE_EXIT_CONFIRM -DLIMIT_WINDOW_SIZE_TO_SCREEN "$@" \
 cpymo/*.c cpymo-backends/sdl2/*.c cpymo-backends/sdl2/cpymo_mac_menu.m \
 -L"$d/lib" -lSDL2 -lavformat -lavcodec -lswresample -lavutil -liconv -lz -lbz2 \
 -framework Cocoa -framework CoreText -framework CoreFoundation -framework CoreAudio -framework AudioToolbox -framework AudioUnit -framework CoreVideo -framework IOKit -framework ForceFeedback -framework Carbon -framework Metal -framework QuartzCore -framework CoreHaptics -framework GameController -framework CoreGraphics -framework CoreMedia -framework VideoToolbox \
 -o "$out"
