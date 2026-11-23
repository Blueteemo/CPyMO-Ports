# CPyMO 1.0.0 构建

## RG35XX / GarlicOS 1.4.9

使用 Linux、Clang 21.1.8、LLD、GNU readelf、LLVM objdump、Python 3 和 MiyooCFW ARM/uClibc SDK。发布构建使用 MiyooCFW/buildroot 的 Actions 运行 `36049439393`、产物 `10833896498`。将 SDK 解压并设置 `SDK` 为含 sysroot 和 GCC 运行库的目录。上游也提供构建 SDK 的说明。

- [已用 SDK 的上游工作流](https://github.com/MiyooCFW/buildroot/actions/runs/36049439393)
- [上游 SDK 构建说明](https://github.com/TriForceX/MiyooCFW/wiki/Build-Image-and-SDK)

在 Git 检出的 1.0.0 源码目录中运行：

```sh
cd platforms/rg35xx
SDK=/absolute/path/arm-miyoo-linux-uclibcgnueabi_sdk-buildroot \
  BUILD=/absolute/path/rg35xx-build bash tools/build_arm_clang_lld.sh softfp
python3 tools/package_rg35xx_release.py /absolute/path/rg35xx-build /absolute/path/output
```

构建产物为 ARM ELF32 Libretro 核心，采用 softfp 调用约定，动态依赖 uClibc。打包脚本校对 Git 提交、源码指纹和核心校验值，并生成 ZIP 及 SHA-256。

## Symbian S60v3

从 `v1.0.0` 发布附件取 `CPyMO_S60v3_1.0.0_SDL_Relink_Handoff.zip`，在 Debian x86-64 的无空格 ASCII 路径下解压。先校验其中的 `SHA256SUMS.txt`，再按包内 `README.md` 恢复工具链。包中的 `EXTERNAL_INPUTS.md` 提供 S60 FP1 SDK、Open C、GCCE 和其他依赖的版本、来源及哈希。SDK 和授权工具由构建者从所列来源准备。

重链接包包含应用源码、头文件、构建和重链接脚本、Symbian SDL 修改及验证记录。它能构建未签名的 ARMV5 E32 EXE，供替换 SDL 后重链接。构建者可使用自己有权使用的启动图，也可沿用脚本默认的纯黑启动帧。

同一发行页的 `CPyMO_S60v3_1.0.0_SDL_Corresponding_Source.zip` 提供已应用实际修改的 Symbian SDL 源码树和 LGPL 2.1 许可。静态链接的 libmad 随应用源码提供，并保留原作者署名和 GPL 许可。[libmad 源码及许可](../platforms/s60v3/engine/source/thirdparty/libmad/)

[实际链接的 Symbian SDL 对应源码与补丁](../platforms/s60v3/engine/sdl_exit_fix/linked-source/SDL_对应源码.md) · [SDL LGPL-2.1 文本](../platforms/s60v3/engine/sdl_exit_fix/COPYING.LGPL-2.1)

正常安装使用发布附件内的签名 SISX。其包内版本为 1.0.0，应用 UID/SID 为 `0xE93C60A3`。
