# RG35XX 游戏视频播放

本目录是初代 RG35XX（GarlicOS 1.4.9）的游戏视频播放实现。Libretro 核心和播放进程分别构建，不混用 uClibc softfp 与 glibc hard-float 的运行库。

- 核心：按 `integrated/build.sh` 的参数，提供 MiyooCFW ARM/uClibc SDK、Clang 及 ARM GNU 汇编／链接工具。安装入口是 `../deploy/Roms/APPS/CPyMO.sh`。
- 播放进程：为 `build-slim.sh` 提供 FFmpeg n2.6 源码、Arm GNU Toolchain 13.3.Rel1、适配的 LeMedia sysroot 与运行库。FFmpeg 精简配置只保留本地 MOV/MP4、H.264 码流处理、AAC 解码及音频重采样。
- 驱动：`driver/` 保留所用模块源码、内核配置和补丁。基线为 [RG35XX Actions 3.10.37 内核](https://github.com/rg35xx-cfw/s500_kernel/tree/81cc77b59250341b9abe90ed4188cde7b7aa1eeb)。
- [FFmpeg n2.6](https://github.com/FFmpeg/FFmpeg/tree/n2.6) 以 LGPL 配置静态链接。公开二进制须附本次构建所用对应源码、许可和可实际执行的重链接材料。
- Actions 用户态硬解库来自 LeMaker 的 LeMedia for Guitar SD v1509 镜像（`LeMedia_For_Guitar_SD_v1509.7z`，SHA-1 `e3f187ac4e2f509382f1a8a7c13847a1ae5b1ca7`），包括 `libalc.so`、`libion.so`、`libvde_core.so` 和 `vd_h264.so`。这些二进制不在源码仓库内，也不在本项目的 AGPL 授权范围内。
