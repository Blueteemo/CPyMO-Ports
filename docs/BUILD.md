# CPyMO Ports 构建

## RG35XX / GarlicOS 1.4.9

1.1.0 使用平台独立的构建入口。见 [RG35XX 视频组件与构建](../platforms/rg35xx/native-playback/README.md)，以及 [RG35XX 安装指南](../platforms/rg35xx/docs/INSTALL_RG35XX.md)。

## Symbian S60v3

从 `v1.1.0` 发布附件取 `CPyMO_S60v3_1.1.0_SDL_Relink_Handoff.zip`，在 Debian x86-64 的无空格 ASCII 路径下解压。先校验其中的 `SHA256SUMS.txt`，再按包内 `README.md` 恢复工具链。包中的 `EXTERNAL_INPUTS.md` 提供 S60 FP1 SDK、Open C、GCCE 和其他依赖的版本、来源及哈希。SDK 和授权工具由构建者从所列来源准备。

重链接包包含应用源码、头文件、构建和重链接脚本、Symbian SDL 修改及验证记录。它能构建未签名的 ARMV5 E32 EXE，供替换 SDL 后重链接。构建者可使用自己有权使用的启动图，也可沿用脚本默认的纯黑启动帧。

同一发行页的 `CPyMO_S60v3_1.1.0_SDL_Corresponding_Source.zip` 提供已应用实际修改的 Symbian SDL 源码树和 LGPL 2.1 许可。静态链接的 libmad 随应用源码提供，并保留原作者署名和 GPL 许可。[libmad 源码及许可](../platforms/s60v3/engine/source/thirdparty/libmad/)

[实际链接的 Symbian SDL 对应源码与补丁](../platforms/s60v3/engine/sdl_exit_fix/linked-source/SDL_对应源码.md) · [SDL LGPL-2.1 文本](../platforms/s60v3/engine/sdl_exit_fix/COPYING.LGPL-2.1)

正常安装使用发布附件内的签名 SISX。其包内版本为 1.1.0，应用 UID/SID 为 `0xE93C60A3`。
