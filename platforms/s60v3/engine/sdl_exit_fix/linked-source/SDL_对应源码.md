# Symbian SDL 1.2.14_ag 对应源码

实际链接的是由 AnotherGuest 的 Symbian SDL 1.2.14_ag 构建的静态库 ESDL_cpymo.lib。基础归档来自 JigokuMaster/symbian-sdl-libs，提交 ff5a4e52ad8762e46ba5c3e15142b879bca0f76f。

- [确切源码归档](https://codeload.github.com/JigokuMaster/symbian-sdl-libs/tar.gz/ff5a4e52ad8762e46ba5c3e15142b879bca0f76f)
- 本次实际下载的归档 SHA-256 为 e467ff5e0ef8290edac9180e283db4546c231e4aedf71ca62dc65469e3a0c00d，与构建环境原归档逐字节一致。
- 实际链接的 ESDL_cpymo.lib SHA-256 为 63ee058134006e86e62b9975fa1f76bd83071ebcb70e81ae7c990d1200234d51。包内提供 LGPL-2.1 文本、源码差异、源码重放验证和实际库成员记录。

## 取得完整的对应源码树

1.1.0 发行附件 `CPyMO_S60v3_1.1.0_SDL_Corresponding_Source.zip` 提供已应用补丁的源文件和 Symbian GCCE 工程。也可按下列步骤从固定的上游归档重放。

1. 下载并校验归档，解压后进入包含 SDL-1.2.14_ag/ 的目录。
2. 在该目录对未修改的树执行 patch -p1 --batch -i /绝对路径/SDL-1.2.14_ag-linked-sources.patch。
3. 得到的 SDL-1.2.14_ag/ 源码树含 Open C 适配和 CPyMO 启动、绘制、前后台切换和视频交接改动。已从原归档实测重放，六个有差异的文件均与实际编译输入逐字节一致。具体 SHA-256 见 SDL_SOURCE_VERIFICATION.json。

## 原构建的补丁顺序

原恢复流程先对 SDL 基树应用 sdl-openc.patch，再在 SDL-1.2.14_ag/Symbian/s60v3/ 执行 bldmake bldfiles 和 abld build gcce urel，得到 ESDL.lib 及 GCCE makefile。CPyMO 的 engine/sdl_exit_fix/build_sdl_exit_fix.py 随后基于原库重编并替换 EBasicApp.o、SDLSXXView.o、SDL_epocvideo.o、SDL_epocevents.o，生成 ESDL_cpymo.lib。脚本同时生成或使用随附的分段补丁和对视图的额外调整。合并补丁已经包含这些改动。用合并补丁重放源码时，直接保留重放后的树。通过原构建脚本复建时，按上述两阶段流程从基础归档开始。

CPyMO 与 Symbian SDL 分别保留 AGPL-3.0 和 LGPL-2.1 的许可声明。应用源码和静态重链接步骤见同一发行页的 `CPyMO_S60v3_1.1.0_SDL_Relink_Handoff.zip`。
