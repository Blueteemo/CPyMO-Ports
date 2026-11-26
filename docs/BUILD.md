# CPyMO Ports 构建

## RG35XX / GarlicOS 1.4.9

1.1.0 使用平台独立的构建入口。见 [RG35XX 视频组件与构建](../platforms/rg35xx/native-playback/README.md)，以及 [RG35XX 安装指南](../platforms/rg35xx/docs/INSTALL_RG35XX.md)。

## Symbian S60v3

从 `s60v3-v1.1.1` 发布附件取 `CPyMO_S60v3_1.1.1_SDL_Relink_Handoff.zip`，在 Debian x86-64 的无空格 ASCII 路径下解压。先校验其中的 `SHA256SUMS.txt`，再按包内 `README.md` 恢复工具链。包中的 `EXTERNAL_INPUTS_1.0.0_BASE.md` 提供 S60 FP1 SDK、Open C、GCCE 和其他依赖的版本、来源及哈希。SDK 和授权工具由构建者从所列来源准备。

重链接包包含应用源码、头文件、构建和重链接脚本、Symbian SDL 修改及验证记录。它能构建未签名的 ARMV5 E32 EXE，供替换 SDL 后重链接。正式发行构建必须使用已定稿的 320×240 启动图。需要替换素材的第三方自建版本可以自行准备有权使用的图片，并明确更新其配套校验值。

同一发行页的 `CPyMO_S60v3_1.1.1_SDL_Corresponding_Source.zip` 提供已应用实际修改的 Symbian SDL 源码树和 LGPL 2.1 许可。静态链接的 libmad 随应用源码提供，并保留原作者署名和 GPL 许可。[libmad 源码及许可](../platforms/s60v3/engine/source/thirdparty/libmad/)

[实际链接的 Symbian SDL 对应源码与补丁](../platforms/s60v3/engine/sdl_exit_fix/linked-source/SDL_对应源码.md) · [SDL LGPL-2.1 文本](../platforms/s60v3/engine/sdl_exit_fix/COPYING.LGPL-2.1)

正常安装使用发布附件内的签名 SISX。其包内版本为 1.1.1，应用 UID/SID 为 `0xE93C60A3`。

### 正式启动图的构建约束

S60v3 已定稿启动图是 `platforms/s60v3/engine/assets/startup.png`，其基准校验见同目录 `startup.sha256`；该文件与正式 1.1.0 安装包 EXE 内嵌的 PNG 逐字节一致。常规版本更新须从当前主分支或已核对的基准源码构建，不允许用历史测试工程覆盖该资源。

`scripts/build_candidate.py` 在编译前检查图片哈希和 PNG 尺寸，链接后检查 EXE 内实际嵌入的图片；`scripts/audit_sisx.py` 再从安装包的 EXE 载荷检查同一份图片。任一步不符均中止。可单独运行 `python3 platforms/s60v3/engine/scripts/startup_asset.py` 或 `python3 platforms/s60v3/engine/tests/host-startup-asset/check_startup_asset.py`。

只有经过明确确认的启动图改版才可修改 `startup.png` 和 `startup.sha256`，不得仅更新哈希来接受错误的旧图。正式发行前还应核对上一个正式安装包中嵌入的资源，确认预期未变的素材确实未变。
