<p align="center">
  <img src="assets/branding/pymo-long-logo.webp" alt="PyMO — Python Memories Off" width="720">
</p>

# CPyMO Ports

**CPyMO 视觉小说引擎的开源移植项目，首发支持 Symbian S60v3 与初代 RG35XX（GarlicOS）。**

使用 Nintendo DS、DS Lite 或 DSi 的玩家，请前往 [CPyMO for NDS](https://github.com/Blueteemo/CPyMO)。

## 下载

| 平台 | 1.0.0 安装包 | 安装要求 |
| --- | --- | --- |
| **Symbian S60v3** | [S60v3 签名安装包](https://github.com/Blueteemo/CPyMO-Ports/releases/download/v1.0.0/CPyMO_S60v3_1.0.0_Installer.zip) | [P.I.P.S. 1.7.0 或更新版本](#symbian-s60v3) |
| **初代 RG35XX** | [RG35XX GarlicOS 安装包](https://github.com/Blueteemo/CPyMO-Ports/releases/download/v1.0.0/CPyMO_RG35XX_1.0.0_GarlicOS.zip) | [GarlicOS 1.4.9](https://www.patreon.com/blackseraph/posts/garlicos-for-76561333) |

[发行说明](https://github.com/Blueteemo/CPyMO-Ports/releases/tag/v1.0.0) · [完整源码](https://github.com/Blueteemo/CPyMO-Ports/releases/download/v1.0.0/CPyMO_1.0.0_Source.zip) · [Symbian SDL 重链接包](https://github.com/Blueteemo/CPyMO-Ports/releases/download/v1.0.0/CPyMO_S60v3_1.0.0_SDL_Relink_Handoff.zip) · [SHA-256 校验清单](https://github.com/Blueteemo/CPyMO-Ports/releases/download/v1.0.0/SHA256SUMS.txt)

游戏文件由玩家自行准备。

## 安装与开始使用

### Symbian S60v3

1. **先装 P.I.P.S.** 在手机上打开下载的 `.sis` 并安装。已装 1.7.0 或更新版本可跳过。[下载 P.I.P.S.（第三方镜像）](https://files.3zet.net/Operation%20Symbian/Libs%20and%20Tools/pips_s60_1_7_SS.sis)
   - 如果安装 P.I.P.S. 时提示证书尚未生效或已过期，请将手机日期临时调到 2015 年，装完后再改回当前日期。
2. **再装 CPyMO。** 解压 CPyMO 安装包，在手机上打开其中的 `.sisx` 并安装。[下载 S60v3 安装包](https://github.com/Blueteemo/CPyMO-Ports/releases/download/v1.0.0/CPyMO_S60v3_1.0.0_Installer.zip)

把游戏文件夹放进 `E:\Python\` 或 `C:\Data\Python\`，从手机菜单打开 CPyMO，选中游戏后按左软键进入。

签名安装选项见包内说明。[P.I.P.S. 来源与 SHA-256](platforms/s60v3/engine/Dependencies/PIPS_1.7.0.txt)

开场图居中版沿用 1.0.0，使用新的自签名证书。如果手机拒绝覆盖安装，先备份游戏目录内的 `save` 文件夹，再通过程序管理卸载 CPyMO 后重新安装。

首发签名包曾在 Nokia 5320d-1（S60v3 FP2）模拟器通过安装、冷启动、进入游戏列表和挂断键退出验证。居中版已校验签名及安装载荷，E63 实机覆盖安装尚待验证。

### 初代 RG35XX

1. 解压 ZIP，将 `Roms/` 下的文件逐项复制到 TF 卡的同名路径，保留卡上原有的文件夹和其他内容。
2. 将每款游戏的完整目录放入 `Roms/APPS/CPyMO/games/`。
3. 在 GarlicOS 的 **APPS** 中打开 CPyMO，从游戏列表选择游戏。

游戏内提供两档字号、宽屏遮罩和两行对白存档预览。[RG35XX 安装指南](platforms/rg35xx/docs/INSTALL_RG35XX.md)

## 源码、作者与许可证

- 平台源码见 [Symbian S60v3](platforms/s60v3/engine/) 和 [RG35XX](platforms/rg35xx/)。[构建与 SDL 重链接](docs/BUILD.md)
- 本移植基于 [CPyMO](https://github.com/Strrationalism/CPyMO)（Seng Jik 及贡献者），其基础引擎来自 [PyMO](https://github.com/pymo/pymo)（chen_xin_ming 及贡献者）。引擎和两端移植按 [AGPL-3.0](LICENSE) 发行，第三方组件保留各自的许可和署名。
- 页首图案取材于 PyMO 原始应用图标。[PyMO 图案来源与署名](docs/third-party/PYMO-ICON-ATTRIBUTION.md)
