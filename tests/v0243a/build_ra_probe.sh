#!/bin/sh
set -eu
root=$(pwd)
src="$root/evidence/retroarch-1.14.0"
obj="$root/probe_build"
mkdir -p "$obj"
for unit in config_file.c libretro-common/string/stdstring.c libretro-common/compat/compat_strl.c libretro-common/compat/compat_strldup.c libretro-common/compat/compat_posix_string.c libretro-common/file/file_path.c; do
    cc -O2 -std=gnu11 -ffunction-sections -fdata-sections -I "$src/include" -c "$src/$unit" -o "$obj/$(basename "$unit").o"
done
cc -O2 -I "$src/include" "$root/repo/tests/v0243a/ra_config_probe.c" "$root/repo/tests/v0243a/ra_posix_io.c" "$obj/"*.o -Wl,--gc-sections -o "$root/ra_config_probe"
