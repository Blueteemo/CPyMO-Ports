<p align="center">
  <img src="assets/branding/pymo-long-logo.webp" alt="PyMO — Python Memories Off" width="640">
</p>

# CPyMO Ports

**CPyMO 视觉小说引擎的开源移植项目，支持 Symbian S60v3 与初代 RG35XX（GarlicOS）。**

使用 Nintendo DS、DS Lite 或 DSi 的玩家，请前往 [CPyMO for NDS](https://github.com/Blueteemo/CPyMO)。

## 下载

请从 [GitHub Releases](https://github.com/Blueteemo/CPyMO-Ports/releases) 按平台下载安装包。发行页同时提供更新说明、校验值和对应源码附件。

## 安装与开始使用

### Symbian S60v3

1. **先装 P.I.P.S.** 在手机上打开下载的 `.sis` 并安装。已装 1.7.0 或更新版本可跳过。[下载 P.I.P.S.（第三方镜像）](https://files.3zet.net/Operation%20Symbian/Libs%20and%20Tools/pips_s60_1_7_SS.sis)
   - 如果安装 P.I.P.S. 时提示证书尚未生效或已过期，请将手机日期临时调到 2015 年，装完后再改回当前日期。
2. **再装 CPyMO。** 解压 CPyMO 安装包，在手机上打开其中的 `.sisx` 并安装。[前往发行页](https://github.com/Blueteemo/CPyMO-Ports/releases)

把游戏文件夹放进 `E:\Python\` 或 `C:\Data\Python\`，从手机菜单打开 CPyMO，选中游戏后按左软键进入。

签名安装选项见包内说明。[P.I.P.S. 来源与 SHA-256](platforms/s60v3/engine/Dependencies/PIPS_1.7.0.txt)

安装包使用 `CPyMO S60v3` 自签名证书。如果手机拒绝覆盖安装，先备份游戏目录内的 `save` 文件夹，再通过程序管理卸载 CPyMO 后重新安装。1.1.0 已在 Nokia E63 上验证。

游戏视频由系统播放器打开；播放完从播放器按返回键继续游戏。

### 初代 RG35XX

1. 解压 ZIP，将 `Roms/` 下的文件逐项复制到 TF 卡的同名路径，保留卡上原有的文件夹和其他内容。
2. 将每款游戏的完整目录放入 `Roms/APPS/CPyMO/games/`。
3. 在 GarlicOS 的 **APPS** 中打开 CPyMO，从游戏列表选择游戏。

游戏内提供两档字号、宽屏遮罩和两行对白存档预览。1.1.0 支持 H.264 视频、AAC 音频的 MP4 硬解播放，需配套运行库。[RG35XX 安装指南](platforms/rg35xx/docs/INSTALL_RG35XX.md)

## 源码、作者与许可证

- 平台源码见 [Symbian S60v3](platforms/s60v3/engine/) 和 [RG35XX](platforms/rg35xx/)。[构建与 SDL 重链接](docs/BUILD.md)
- 本移植基于 [CPyMO](https://github.com/Strrationalism/CPyMO)（Seng Jik 及贡献者），其基础引擎来自 [PyMO](https://github.com/pymo/pymo)（chen_xin_ming 及贡献者）。引擎和两端移植按 [AGPL-3.0](LICENSE) 发行，第三方组件保留各自的许可和署名。
- 页首图案取材于 PyMO 原始应用图标。[PyMO 图案来源与署名](docs/third-party/PYMO-ICON-ATTRIBUTION.md)
