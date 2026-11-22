# v0.24.5 实机反馈修订验证

在 Linux QA 目录准备 repo、fixtures、hostbuild、asanbuild、build-arm、capture.host/capture.arm，构建方式见 tests/v0245 和 tools/build_arm_clang_lld.sh。沿用 v0.24.5 真实游戏及私有旧档环境变量，禁止直接以 TF 卡作为运行和保存目录。

- integration.py host / arm：真实输入与输出帧，5场景；往返30次后同一选择逐像素一致，4行三行标题边界，新小号持久化，START导航，单次B关闭内容。
- render_check.c：AddressSanitizer/UBSan，原有字形/混色/增益检查和新3画布×3字号菜单边界、UI_1/UI_2/UI_3迁移与12字节config.csav检查。该文件直接包含 libretro/text/msgbox/backlog/config_ui/textbox/select_img/rmenu 实现，链接其余 asanbuild 对象和 -lm，避免重复定义。参数为字体路径，CPYMO_QA_GAME_PATH 为私有540x360 fixture目录。
- audio_rollback.py：CPYMO_QA_BASELINE 指向 v0.24.4 源码，CPYMO_QA_AUDIO_INDEX 为 v0.24.5 audio-real/selected.json（其source路径需存在），20组真实音源/循环/混音的PCM逐字节回退检查。无声卡听感结论。
- async_ui_probe.c：以 tests/v0245/build_harness.py 的 host 编译参数加 -pthread，链接 -ldl -lm；参数 CORE GAME SAVE [batch]。用独立前端音频回调，在10次80ms模拟UI停顿中核查音频进度；batch参数仅用于旧v0.24.5强制同步核心负对照。只是恢复v0.24.4路径的行为测试，不是新增线程实现，也不证明硬件无卡顿。
- tests/v0245_launchfix 下146场景继续运行；唯一预期变化为日志修订号。启动器仅在本次临时session.cfg追加 quit_on_close_content=1（RA1.14枚举），用未改上游解析器核查，不写全局配置。

继续运行 tests/v0245 的 suite/extended/actual_games（host、ARM、ASan按对应脚本使用），新小/默认/大内部值为2/0/1，旧small=0现在仍为0且仅改名默认。旧 async_policy.py 针对被撤回的“强制同步音频”实验，本版本不适用，以本目录 async_ui_probe 正负对照替代。

软件测试不替代真机声音、光标观感和返回GarlicOS验收。新字体设置写UI_3，回退旧核心时旧核心不能识别这份UI设置；可恢复备份的 rg35xx-ui.cfg 或重新选设置，剧情存档格式没有改变。
