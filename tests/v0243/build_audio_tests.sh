set -eu
src=repo/source/CPyMO-main
flags='-O2 -std=gnu11 -DDISABLE_MOVIE -DDISABLE_FFMPEG_AUDIO -DGARLIC_NO_FFMPEG -ffunction-sections -fdata-sections'
gcc $flags repo/tests/v0243/audio_stream.c "$src/cpymo/cpymo_str.c" "$src/cpymo/cpymo_package.c" "$src/cpymo-backends/libretro/stb_vorbis.c" -Wl,--gc-sections -lm -o audio_candidate
gcc $flags -DCPYMO_AUDIO_BASELINE -DAUDIO_SOURCE="\"$(pwd)/baseline/source/CPyMO-main/cpymo-backends/libretro/cpymo_audio_mp3.c\"" repo/tests/v0243/audio_stream.c "$src/cpymo/cpymo_str.c" "$src/cpymo/cpymo_package.c" "$src/cpymo-backends/libretro/stb_vorbis.c" -Wl,--gc-sections -lm -o audio_baseline
