# CPyMO RG35XX 1.1.0 安装指南

适用于运行 GarlicOS 1.4.9 的初代 RG35XX。游戏由玩家自行准备。 [GarlicOS 1.4.9 发布页](https://www.patreon.com/blackseraph/posts/garlicos-for-76561333)

1. 解压安装包，把其中 `Roms/` 下的文件复制到 TF 卡上的同名路径。
2. 把每款游戏的完整目录放入 `Roms/APPS/CPyMO/games/`。
3. 安全弹出卡，插回掌机，在 GarlicOS 的 **APPS** 中打开 CPyMO。

从 1.0.0 更新时可直接覆盖同名程序文件。游戏、存档和音量等个人配置请保留。

## 原片播放

游戏启用视频时，CPyMO 可以直接播放位于游戏 `video/` 目录的 H.264 视频、AAC 音频 MP4。使用硬解需要相匹配的 Actions/LeMedia 运行库；见构建说明。不要将不明来源的库混用。播放时按 B 或 A 可跳过并回到游戏。

实机已验证 800×600 片尾完整播放，以及 720p 片头播放中按键返回。播放进度以音频为基准，720p 可能跳过迟到的画面。
