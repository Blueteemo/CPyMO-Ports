# RG35XX v0.24.4 本地实机测试候选

目标为初代 RG35XX、GarlicOS 1.4.9、ARM/uClibc soft-call EABI、Cortex-A9/VFPv3 SOFTFP。不是 H700/Plus 版本。本轮源码基于 `3c086317118010d343f8884149cc5372350bac5b`；完整历史包含 v0.24.2 的 `c82881c…` 和 v0.24.3 的 `78b2d41c…`。旧分支保留，v0.24.3a 启动器、音量继承、总 PCM 增益、图标与按键映射未改。

## 游戏库生命周期

正常启动总是显示游戏库，单游戏也要确认选择。列表 B 退出核心；游戏菜单新增“返回游戏列表”及确认提示；游戏自然结束、脚本加载失败及损坏的 AVI 返回列表。列表没有共享字体时，可使用所选游戏/首个游戏自带字体。`CPYMO_MENU=0` 仅保留为明确指定的直接加载诊断模式。

菜单回调仅提交游戏路径或返回请求。`retro_run()` 在 `cpymo_engine_update()` 返回后，统一释放引擎和 UI、音频、图像、字体及当前工作目录；新引擎从零初始化，按键排空到全部松开。重新开始游戏也走这条路径。每部游戏仍使用原存档隔离规则。

## SE 保存语义与旧档

旧 RG35XX MP3 后端 `get_se_name()` 返回所有 SE 的保留名称，包括一次性声音及停止后的名称。传统 csav 的该字段只表示需要恢复的循环 SE，读档时使用 `loop=true`。这会将《水仙1》`NaruC1:92` 的一次性 `faan1_b` 恢复为无限循环。

本版仅保存仍在播放的循环 SE，不修改 csav 布局。旧档读入后，对当前游戏的脚本进行有界扫描：只有该名称明确作为一次性音效使用，且全部脚本中没有循环用法，才撤销错误恢复。循环、混合用法、扫描不完整或不可读时保留原语义。扫描上限为 512 个脚本、单文件 8 MiB、总计 32 MiB；不重写旧存档。不通过修改游戏脚本实现修复。

已停止的 BGM 不再把保留名称写入新存档，避免读档后恢复本应保持安静的旧曲。引擎释放时还会释放各音频通道的 64 KiB 环形缓冲和 16 KiB 重采样表，而非在下一次初始化时丢失其指针；正常播放期间的复用和解码算法不变。

还修正了普通 `#se` 错误参考 Voice 音量的通道判断，使关闭语音时仍能独立播放 SE。循环环境音继续可在静音状态保持时钟，并由脚本停止。

## 字体、已读与设置

大档保留原 100% 大小，小档改为 86%，默认小档。切换时重排当前正文及重建当前名字字形，不重走 say、追加历史或推进剧情。名字字形通过历史记录原有的所有权接口转交，旧历史的名字仍有效。

“已读变灰”默认关：已读与未读保留脚本原色和正常不透明度；开启后，只对真实已读哈希命中的文字使用 0.75 Alpha，当前页立即刷新。已读记录与仅已读快进仍共用原哈希状态。

UI 共七项：BGM、SE、Voice、文字速度、字体大小、快进模式、已读变灰。三个音量在 0/100% 停止，离散开关仍可循环。原 12 字节 `config.csav` 不变；`rg35xx-ui.cfg` 升为 `CPYMO_UI_2`，读入兼容旧 `CPYMO_UI_1`。不加入语言切换或未经验证的立绘特效开关。

共享字体仅补齐覆盖范围。基于 [Adobe Source Han Sans 2.005R](https://github.com/adobe-fonts/source-han-sans/releases/tag/2.005R) 的同一 SC Medium 字体，保留原字体的垂直度量及 GPOS/kerning 策略，加入游戏配置、脚本和 UI 所需的 30 个码位。旧 7,170 个码位在六个像素尺寸、四个子像素位置下的 172,080 个实际 stb 字形位图逐字节一致，9,025 对 ASCII kerning 亦一致。新字体共 7,200 个码位，覆盖两部游戏的配置与脚本；导出名称改为 CPyMO CJK Medium，并附 OFL 原文。`tools/build_rg35xx_font.py` 和 `tests/v0244/font_parity.c` 可复现。

## 视频实验路径

原构建禁用 FFmpeg movie，不能直接解码原 H.264/AAC MP4。本版复用既有 `stb_image` JPEG 解码器，新增严格受限的 MJPEG/PCM AVI 播放 UI。原视频不覆盖；`#movie opening` 优先读取同目录的 `opening.rg35xx.avi`。无 sidecar 时明确记录跳过，不能视为原 MP4 已兼容。损坏 sidecar 返回游戏库。

使用现有 FFmpeg 生成 sidecar：

```sh
python3 tools/convert_rg35xx_movie.py /path/to/video/opening.mp4 /separate/test-copy/video/opening.rg35xx.avi --ffmpeg /path/to/ffmpeg
```

转换保持比例、最大 480×480、24 fps、MJPEG q=5、PCM s16le/48 kHz/双声道。播放器接受有限 RIFF AVI：视频流 00/MJPG、音频流 01/PCM、最大 640×480/60 fps/10 分钟、文件不超过 512 MiB。拒绝越界、异常尺寸、压缩帧过大及不支持的流；不预载整段影片，仅保存包索引、当前 JPEG/RGB 和统计。

声音样本时钟驱动画面；前端静音时仍推进。A/B 跳过，UI 删除/游戏退出时统一关闭影片并释放内存。视频替代普通混音，沿用 BGM 音量、核心 PCM 总增益及前端系统音量。日志区分 completed、skipped、failed，记录解码均值/P95/峰值、帧数、掉帧、样本时钟差、读取错误和直接拥有的内存。直接拥有的字节数不等于进程峰值 RSS；QEMU 指标不等于硬件实时性能。

## 图像缩小

比较最近邻、双线性及面积采样后，缩小最终合成的图像时使用 Q8 整数面积权重。透明素材先按原流程合成，随后缩图；高清文字在缩图后单独合成。相同尺寸、放大及不支持的特殊几何保留原路径。原宽高比及《水仙1》的黑边不变。缩图工作只发生在实际重绘帧；800×600 真机开销仍须测量。

## 构建与验证

SDK 为随 v0.24.1 交接的 MiyooCFW/uClibc SDK，原 ZIP SHA-256 为 `b15eaefc9aab786634da5865bc8796ad99bd78128da287fb272ad659a98e56a5`。Linux 主机准备 clang/lld、readelf、llvm-objdump、gcc、qemu-arm、Python/Pillow。

```sh
SDK=/path/to/arm-miyoo-linux-uclibcgnueabi_sdk-buildroot BUILD=/clean/output bash tools/build_arm_clang_lld.sh softfp
python3 tools/verify_uclibc_core.py /clean/output/cpymo_libretro.so
```

QA 使用独立根目录，内含本源码的 `repo/`、基线 `baseline/`、`hostbuild/`、`asanbuild/`、`build-arm/` 和合成 `fixtures/`。设置 `SDK`、`CPYMO_QA_FONT`、`CPYMO_QA_YUME`、`CPYMO_QA_SHORT_MOVIE`、`CPYMO_QA_WATER` 为实际路径：

```sh
python3 repo/tests/v0243/make_fixtures.py fixtures "$CPYMO_QA_FONT"
python3 repo/tests/v0244/build_harness.py
python3 repo/tests/v0244/suite.py host
python3 repo/tests/v0244/suite.py arm
python3 repo/tests/v0244/extended.py host --sanitize
python3 repo/tests/v0244/audio_matrix.py
```

集成观察器通过真实 Libretro 回调输入按键；从 ELF 符号偏移只读观察引擎和音频状态，不改引擎内存。仅选定帧输出图像，音频只统计样本/峰值/RMS，不保存整段 PCM。完整《梦见之药》开场、真实《水仙1》存读档、最终核心字节与各场景回执见独立工程交付的最终验收报告和日志。所有结论需区分主机、QEMU 与用户之后的 RG35XX 实机验收。
