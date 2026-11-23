# PyMO 图案来源与署名

人物、黄色吉祥物和手柄的图案母题来自 PyMO 原项目的应用图标。原项目保留了 PNG 与 PSD，版权声明为 Copyright (c) 2013 pymo，原 MIT 文本随本文件附带。本系列高清插画是用户提供并在 NDS 项目选定的 AI 辅助衍生图。保留 PyMO 作者署名及原项目许可。插画的使用不代表 PyMO 原作者参与或背书。

- [PyMO 原始图标 PNG](https://github.com/pymo/pymo/blob/703ba457fba90635ab61fe4c8ec054e002613283/images/pymo-icon.png)
- [原始 PSD](https://github.com/pymo/pymo/blob/703ba457fba90635ab61fe4c8ec054e002613283/images/pymo-icon.psd)
- [PyMO 原项目 MIT 许可证](https://github.com/pymo/pymo/blob/703ba457fba90635ab61fe4c8ec054e002613283/LICENSE)
- [随附许可证文本](PYMO-ORIGINAL-MIT-LICENSE.txt)

## 平台派生

- NDS 1.3.0 的桌面转换器使用这张母图的圆角派生图，NDS 系统图标、开场和列表占位来自方形母图。NDS 的方形母图与 S60v3 母图的 PNG 编码不同，解码后的 1254×1254 RGB 像素完全相同。该项目的公开源码和发行说明已附同源署名。
- S60v3 的 `platforms/s60v3/engine/assets/app-icon.png` 是母图。生成脚本从经确认的区域裁切并生成 MBM 各尺寸图标。60×60 的 `startup-icon.png` 是开场画面头像组件，`startup.png` 在黑底上组合头像和本项目辅助设计的 CPyMO 字标。
- RG35XX 的 `platforms/rg35xx/deploy/Roms/APPS/Imgs/CPyMO.png` 在 APPS 图标中使用同一方形插画。本地已从 v0.24.2 档案复现 v0.24.3 的 190×190 插画调整，当前图标解码像素与复现完全相同。v0.24.2 的 239×239 插画内区与同一母图经 Lanczos 缩小后的像素完全相同。RG35XX 的开场 PNG 使用同一头像和字标组合。
- 两端的 320×240 开场图按各自显示结果做整数像素平移。S60v3 向左移 8 px，RG35XX 向左移 7 px。解码后图案像素相同。

这份记录为图案、母图和平台派生提供可核对的来源。首发时应在源码和发行附件旁附原 MIT 文本与本署名说明，并注明开场字标来自本项目制作记录。
