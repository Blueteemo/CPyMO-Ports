# CPyMO original RG35XX / GarlicOS/uClibc v0.21 — PERFORMANCE / FONT / DIAGNOSTICS

**Hardware baseline:** Physical RG35XX running GarlicOS 1.4.9/uClibc, RetroArch 1.14.0. The user successfully launched v0.20 ARM Libretro core (2m25s and exit code 0); observed very poor frame rate and boxed menu characters. v0.21 is a new ARM candidate **NOT yet tested on that machine**. The original glibc/Stock CFW `.dge` is incompatible with this firmware, do not use.

## Changes relative to v0.20

1. RGB565 nearest pixel-centre scaling: X coordinates lookup once per image, avoiding approx 307,200 div operations per 640x480 repaint. 140 pixel-for-pixel host parity tests passed; native host microbenchmark approx 2.1–2.5x faster for this *one* stage; does not predict whole-game speed.
2. CPYMO_PERF metrics every 180 frames: real elapsed, effective `wall_fps`, update, software draw, RGB565 conversion average (us). Real device log needed to find dominant bottleneck. Host profiling points to SOFTWARE DRAW (not RGB565 alone) as likely expensive stage.
3. Replaced minimal 62KB font test subset with 95KB Noto Sans CJK SC UI subset containing all built-in CPyMO localization Unicode characters (including Simplified/Traditional variants). This addresses CPyMO's built-in UI; RetroArch UI may have separate font problems. Real-game needs full CJK font.
4. Added synthetic `system/sel_highlight.png` for menu selection, addressing exact warnings found nine times in v0.20 log.
5. Added a local DeepSeek/Codex reproducibility handoff (see `docs/本地DeepSeek开发与模拟验证指南.md`), optional ARM/uClibc QEMU user dlopen probe, original real hardware log, helper log parser and tests.
6. Complete ARM/uClibc core rebuilt with SDK, linked, validated. No glibc dependencies or user firmware changes.

## Hardware testing

Copy **only** `deploy/Roms/APPS/CPyMO_RG35XX_uClibc_Test.sh` and `deploy/Roms/APPS/CPyMO_RG35XX_uClibc_Test/` onto your existing TF card under `Roms/APPS/` (or use the separate `CPyMO_RG35XX_GarlicOS_uClibc_SmokeTest_v0.21.zip`). Overwrite only these two test-specific paths; leave original glibc v0.18 files alone. No system partitions, existing ROMs, dmenu or RetroArch system config touched. Start APPS test, proceed through logos and sample dialogue, open CPyMO's in-game menu as before, let it run ~1-2 minutes, exit normally. Afterward send `Roms/APPS/CPyMO_RG35XX_uClibc_Test.log`. The important new lines start with `CPYMO_PERF`.

To compare whether boxed glyphs are inside CPyMO or in RetroArch's own overlay, a photo helps if still present.

## Reproducible builds & local emulator

Source: `source/CPyMO-main/`. SDK: official MiyooCFW Actions run `36049439393`, artifact `10833896498` (`SDK (uClibc)`, build 2026-09-24). Its ~1GB extracted sysroot is not bundled. Run `relocate-sdk.sh` after unpacking.

```sh
CC=/absolute/path/arm-miyoo-linux-uclibcgnueabi_sdk-buildroot/bin/arm-miyoo-linux-uclibcgnueabi-gcc \
BUILD=/tmp/cpymo_arm21 bash tools/build_garlicos_uclibc.sh
bash tests/run_stage_v21.sh
python3 tools/parse_device_perf.py /path/to/CPyMO_RG35XX_uClibc_Test.log
```

Optional Linux/WSL2 QEMU user + SDK: `CC=$CC bash tools/run_qemu_arm_probe.sh` after installing `qemu-user`. This only validates library loading / ABI in a Linux process. It DOES NOT simulate RG35XX framebuffer, controller, ALSA or RetroArch timing. For host rendering+gameplay checks, use existing Libretro fake frontend tests (in `tests/run_stage_v21.sh`). See docs for full detail and a ready-made prompt for local DeepSeek.

## Limitations

No hardware performance measurement for v0.21 yet. No BGM/SE/VO or video. RetroArch save states are unsupported, in-engine save is separate. Full game CJK font and actual commercial PyMO title compatibility still need work. `target/V020*` retained for history, while `target/V021*` is current evidence.
