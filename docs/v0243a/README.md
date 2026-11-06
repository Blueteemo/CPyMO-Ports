# RG35XX v0.24.3a：启动时继承 GarlicOS 独立音量

本轮唯一生产文件变更：`deploy/Roms/APPS/CPyMO.sh`。
基线：v0.24.3，78b2d41cea4e7216e511d6d967c652b75290c541。
核心不重编译，继续交付 SHA-256 `7d7f1164423a3c5da3352b48deda547fef1bd23ecf525be3786fe1a66274f971`；核心内显示版本仍为 0.24.3，启动日志额外标识 launcher_revision=0.24.3a。

## 依据

用户附加的 TF 卡只读调查报告中，CPyMO.sh 指纹 aacd4a5582ec30464c2b8d5a31a971983be8e5441f00a685b14c9e0333079b03 与本分支父提交完全相同。卡上核心为 v0.24.2，报告的 8d79b85c... 与已保留 v0.24.2 基线一致。报告描述 volume.cfg 为 -2 dB，全局为 +1.5 dB，APPS 跳过前端 writeRetroArchConfig 合并。云端确认了启动器缺少加载；没有拿到卡内前端/RA 二进制，反汇编地址据该报告，不声称云端重新反汇编过。

## 行为

- 只读 `$RA_ROOT/.retroarch/volume.cfg`（默认 `/mnt/mmc/CFW/retroarch/.retroarch/volume.cfg`），原全局配置的选择逻辑保留。
- 临时目录位于 `/tmp/cpymo-ra.XXXXXX`，目录 0700、文件 0600；一次最多读取 1025 字节，超过 1024 或超过 16 行即拒绝。
- 只接受唯一的 `audio_volume = "十进制数"`，可有空行、普通 # 注释、CRLF、制表符和行尾注释。数值要求有限十进制、最多三位整数/六位小数，范围 [-80,12] dB，与 RA 1.14.0 音量键边界一致。拒绝重复、未知键、include/reference、NUL/非 ASCII、指数、NaN/Inf、注入文本。
- 不 source/eval 原文件，也不把原文件直接传给 RetroArch。仅把规范化六位小数写入本次临时配置。文件缺失或拒绝时回退到 RA 0 dB，不恢复过时正增益；CPyMO 最终 PCM 百分比实现/输出文件完全不变，正常仍为 50%。
- 使用一个 `--appendconfig` 参数，其值为 `CPyMO/retroarch-cpymo.cfg|临时/session.cfg`；已有专用配置不存在时只传临时配置。
- 最后一个附加文件覆盖前面配置，并包含 `config_save_on_exit=false`、`auto_overrides_enable=false`，只对 CPyMO 当前进程生效。阻止之后自动 core/content override 再盖回旧音量或打开退出保存。调查未发现现有 CPyMO override；不影响其他模拟器，input remap/core options 的其他机制不改。
- 子进程运行期间保留临时配置；正常/错误退出与 HUP/INT/TERM 时清理。信号转发并等待子进程结束。临时文件准备失败时退出 47，不带着未知高音量继续启动。SIGKILL/断电不能由 shell 捕获；留下的唯一临时目录位于 /tmp，不会被下次启动重用。
- 启动器不写两份全局音量配置；卡内修改版 RA 的音量键自行保存 volume.cfg 属于原固件功能，本轮保留，不能把 config_save_on_exit=false 解读成禁用它。

## 上游证据和测试边界

RetroArch v1.14.0：retroarch.c 4709 行附近的多次 --appendconfig 会替换前值；configuration.c 3392–3414 按 | 顺序追加；config_file.c 775–806 追加文件优先，文件内重复键第一次优先；configuration.c 3984–4089 自动 overrides 会后置叠加；runloop.c 5694 按 auto_overrides_enable 决定是否调用；command.c 925–926 音量范围 -80 到 +12。

实际编译未修改的 RA config_file.c 和依赖源，以 stdio 适配文件读取。114 个配置/启动/异常/重启场景通过，使用真实解析器核对最终音量、两个保护项；生产脚本在 dash 和 BusyBox ash/awk 各跑格式矩阵。另通过旧启动器回归。宿主 recorder 不运行音频设备或加载核心，因此这不等于实际 GarlicOS 修改版 RA 的集成/扬声器验收。

源码、测试、逐项记录及上游 Git blob 清单在本轮工程增量包。v0.24.3 的字体、图标、快进、BGM 工作不重开，验收结论继续引用原版报告。
