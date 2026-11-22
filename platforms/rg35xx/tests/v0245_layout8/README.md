v0.24.5 output8：正文额外内缩0.4375em→0.375em（约再左移2输出像素），人名及垂直位置不变。START时钟以字形可见边缘补偿左右bearing，左/上使用相同逻辑边距。三画布×三字号×00:00/12:34/23:59实际像素边距差不超过1.1px，底部留白继续保留。

run_render.py：真实RGB565/文本层渲染、墨迹/分页/人名、三档字号、START十行边界、存档提示、logo1/2跳过回归；ASAN+UBSAN。
preview.c：同一生产渲染器和SYSTEM菜单皮肤生成三档正文/START预览，不含游戏私有画面或正文。
observer_thread.c：音频线程只向内存追加10秒PCM；主线程读取已发布且永不覆盖的前缀并保存WAV。ASAN+UBSAN及TSAN独立验证并发、容量边界、样本不变、定时器回卷。
async_core.c：真实核心异步回调，12次重载、108次字号切换，主线程停顿、PAK音效和卸载后回调；额外验证有限长WAV导出。全部存档进入临时目录。
launcher_env.py：dash/BusyBox ash八种输出音量输入，诊断环境变量路径和系统音量保护。

Linux：python3 tests/v0245_layout8/run_render.py ASAN_OBJECT_DIR FONT FIXTURE。
实际扬声器听感仍需真机；软件通过不能视为音乐炸音已修复。
