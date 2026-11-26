# CPyMO for macOS

本目录记录已上机试用的 **0.3.6** 源码基线。不改变现有平台，不代表已经公开发版。工具栏沿用 0.3.6 的原生分组，顶栏对照演示不在本目录。

## 在 Mac 上重建

需要 Apple Silicon、macOS 27.2 或更新版本及对应的命令行工具 / SDK。在本目录运行：

```sh
./build-deps.sh
./build-app.sh
```

依赖脚本下载并核对 SDL2 2.32.10 与 FFmpeg 6.1.3 源码，构建静态依赖；FFmpeg 不启用外部依赖或网络支持。离线可将这两份已下载的档案放进 `build/dependencies/archives/`，按 `dependencies.sha256` 校验后再运行。已有相同版本和配置的静态依赖可用 `./build-app.sh /absolute/path/to/prefix /absolute/path/to/output`。

默认产物是 `dist/CPyMO.app`，本地临时签名，未公证。脚本会检查签名并跑列表 / 网格选择逻辑测试。安装与导入见 [先读我](USER-GUIDE.txt)。引擎保存路径仍在游戏目录内，验收请用副本。

## 来源与许可

- 游戏库由千岚之夏（Blueteemo）开发，使用 SwiftUI 与 AppKit。
- 引擎基于 [CPyMO Aria 1.1.9](https://github.com/Strrationalism/CPyMO/releases/tag/v1.1.9)。`source/engine/` 是本次 Mac 构建对应的源码，保留 Aria 许可证与第三方头文件内的原始署名。NDS、RG35XX、S60v3 仍使用各自的源目录。
- Mac 应用图标沿用 NDS 桌面转换器图标，字标取自同项目的 RG35XX 开场图；见 [PyMO 图案来源](../../docs/third-party/PYMO-ICON-ATTRIBUTION.md)和 [原 MIT 许可](../../docs/third-party/PYMO-ORIGINAL-MIT-LICENSE.txt)。SDL2、FFmpeg 许可证分别在 `licenses/`。苹方从本机加载，不随源码或产物分发。
- 分支不收录安装包、依赖压缩包、游戏、存档或预览数据。对应本地验收包 `CPyMO-Mac-0.3.6.zip` 的 SHA-256：`1425efa49223fdd429ebfc306908d47d6fd11ff472b02c933fbc5d7636cb9da5`。该值只标识当时的包，不要求重建后的签名或 ZIP 逐字节相同。
