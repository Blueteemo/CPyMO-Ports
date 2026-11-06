# v0.24.5 可重复验收

这是开发验收工具，不是用户转换器。测试不修改 TF 卡；真实素材和旧档由用户在工程之外提供，所有存档回放在独立副本中运行。数字音频与 QEMU 时间均不能代替实体机验收。

## 环境与构建

Linux ARM64 QA 环境使用 GCC、Clang/lld 21.1.8、qemu-arm 10.2.1、Python 3 + NumPy + Pillow，以及 MiyooCFW ARM/uClibc SDK。SDK 归档及 SHA 在工程包中。先解包 SDK 内的 tar.gz，并在一个新的可写 QA 目录工作：

```sh
export SDK=/绝对路径/arm-miyoo-linux-uclibcgnueabi_sdk-buildroot
export CPYMO_QA_FONT=/绝对路径/repo/deploy/Roms/APPS/CPyMO/system/default.ttf
export CPYMO_QA_WATER=/绝对路径/水仙540原始资源副本
export CPYMO_QA_YUME=/绝对路径/梦见之药800原始资源副本
export CPYMO_QA_CARD_SAVES=/绝对路径/两游戏旧档的私有副本
export CPYMO_QA_BASELINE=/绝对路径/v0244源码
export CPYMO_QA_243_CORE=/绝对路径/v0243a核心.so
export CPYMO_QA_244_CORE=/绝对路径/v0244核心.so
export CPYMO_QA_BACKGROUND=/绝对路径/安全的800x600背景.jpg
python3 repo/tests/v0243/build_core_gcc.py repo hostbuild --host
python3 repo/tests/v0243/build_core_gcc.py repo asanbuild --host --sanitize
BUILD="$PWD/build-arm" bash repo/tools/build_arm_clang_lld.sh softfp
python3 repo/tests/v0245/build_harness.py
python3 repo/tests/v0243/make_fixtures.py fixtures "$CPYMO_QA_FONT"
```

工程包中当前推荐 ELF 独立存放，不覆盖历史 deploy 目录中的旧测试 ELF；该目录保留了早期历史布局，不可整体当作安装包复制。

## 集成、内存与性能

```sh
python3 repo/tests/v0245/suite.py host
python3 repo/tests/v0245/suite.py arm
python3 repo/tests/v0245/extended.py host
python3 repo/tests/v0245/extended.py arm
python3 repo/tests/v0245/extended.py host --sanitize
python3 repo/tests/v0245/actual_games.py host
python3 repo/tests/v0245/actual_games.py arm
python3 repo/tests/v0245/actual_games.py host --sanitize
python3 repo/tests/v0245/library_layout.py arm
python3 repo/tests/v0245/performance.py
```

每次自动建立 `runs/时间戳/`，保存时间线、命令、环境、退出状态、进程 RSS、每帧耗时分布和阶段日志。观察器只读真实 ELF 状态；真实 R2/L2/Select 输入操作负责存读档及设置。不直接伪造引擎状态。旧档用私有副本，不能把提供旧档的根目录指向正在使用的 TF 卡。

`performance.py` 安全背景+合成透明椭圆、实际 efmsk_07、两部原始游戏回放，共 30 案例；quiet 模式关闭截图、逐帧状态和 RMS 观察。`CPYMO_QA_PERF_VARIANTS=v245` 可只复测候选，`--mask-only` 专测遮罩。P95 是 retro_run（含空闲帧、解码、混音、测试视频回调）的分位数，不是实机 FPS。FIRST_SCENE 是第一次提交有非黑背景的实际帧；脚本 logo 等固有等待未删。JPEG/PNG/读盘/预缩图/PAK/脚本等计时仅 CPYMO_DEBUG=1 时启用。

`render_check.c` 链接 ASan 对象但排除 `_libretro.o`、`_cpymo_backend_text.o`、`_cpymo_msgbox_ui.o`、`_cpymo_backlog.o`、`_cpymo_config_ui.o`、`_cpymo_textbox.o`（这些源码已直接 include）；沿用主机构建宏，加 `-fsanitize=address,undefined -lm`。运行参数为共享字体路径。覆盖物理字形、三画布分页、读灰/颜色/增益、遮罩逐字节完全一致以及 LRU 持有者存活与清理。`scaling_check.c` 仅排除 `_libretro.o`，传入800×600 RGB24文件，对照独立面积积分与透明边缘，生成三种滤镜的安全背景图。

## 数字音频

```sh
python3 repo/tests/v0245/audio_quality.py final
python3 repo/tests/v0245/prepare_real_audio.py
```

标准正弦使用实际 Q16 源时钟拟合；通带增益和残差分开，不能把增益下降误报为低失真。相位511统计单列。支持8/16/22.05/24/32/44.1/48/88.2/96kHz，单/双声道。

真实音源先扫描两游戏 BGM 前8秒频带功率，选各一首高频和低频较多曲目，再解码 Voice/SE/循环雨声，保留源与解码摘要。以这些原始解码 S16 片段为输入，FFmpeg7.1.5 生成参考：

```sh
ffmpeg -f s16le -ar 原采样率 -ac 2 -i 片段.decoded.s16le \
  -af aresample=48000:filter_size=64:phase_shift=10:cutoff=0.97 \
  -f s16le 曲目.reference.s16le
export CPYMO_QA_AUDIO_REFERENCE=/绝对路径/参考目录
python3 repo/tests/v0245/real_audio_compare.py
python3 repo/tests/v0245/audio_loop_checks.py
python3 repo/tests/v0245/arm_audio_budget.py
```

参考目录同时包含 prepare_real_audio.py 的 selected.json。比较脚本以64点 windowed sinc 对齐实际 Q16 时钟与 FIR 延迟，保留左右声道；前0.1秒启动淡入和末端排除。三路混合测试正常音量和全部100%压力；不修改限幅公式或50%增益。`audio_loop_checks.py` 包含旧96kHz EOF 的 ASan 反例、十秒周期 WAV、限幅 DC 和一小时“音频时长”的非实时生成（输出直接丢弃，并非设备连续运行一小时）。`audio_matrix.py` 逐样本摘要对比全部36首原MP3以及3种WAV和可选 Ogg；可用 CPYMO_QA_OGG 指定 Ogg 测试源。

FFmpeg仅为研发参考，不作为新的用户转换流程。交付不含第三方游戏素材、用户旧档或实际音源 PCM；自行提供合法持有的素材后可重复测试。
