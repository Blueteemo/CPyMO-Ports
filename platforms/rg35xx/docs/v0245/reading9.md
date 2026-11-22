# v0.24.5 reading9

根据用户真机反馈，以旧小72%为基准，默认小幅提高到74%；保留小72%，移除旧默认80%及大88%。用户选择固定字重400，以小字号下更实的笔画试用；来自此前同一官方Source Han Sans SC可变字体，固定轴值400、裁到相同7500字，继承OFL并改名CPyMOReading400。

UI sidecar版本升为CPYMO_UI_4。UI_1/2/3的旧字号角色统一迁移为新默认74%，保留read_gray；UI_4区分小72%与默认74%。程序不重写原剧情存档或旧config.csav结构，下一次正常保存UI时再写UI_4。部署前备份原UI偏好，旧核心回退时可恢复这些偏好，不覆盖后续剧情存档。

START时间字号由.045H增至.048H，继续以真实墨迹左界/顶界保持相等边距；底板十行和底部留白保留。游戏列表指南18→19，原白色及左边距、基线保持。

撤销Garlic整段Logo跳过的专用按钮状态、函数与__garlic_boot_end标签，恢复上游行为：A结束当前等待/渐变，不直接跳到首段剧情。上游Android CMake引用同一cpymo库与SDL2前端，默认Logo渐变300ms、Logo各停留300ms、初始等待50ms；SDL2按真实毫秒间隔推进。RG35XX脚本参数与之相同，保持一致，不统一加速游戏转场。此为源码对照，没有安卓设备实测。
来源：https://github.com/Strrationalism/CPyMO/blob/main/cpymo/cpymo_script.c
https://github.com/Strrationalism/CPyMO/blob/main/cpymo/cpymo_interpreter.c
https://github.com/Strrationalism/CPyMO/blob/main/cpymo-backends/android/app/src/main/cpp/CMakeLists.txt
https://github.com/Strrationalism/CPyMO/blob/main/cpymo-backends/sdl2/main.c

实际梦见start脚本另外包含logo2直接显示、800ms等待、450ms splash渐变及1400ms停留。它属于有名游戏脚本，不在原Garlic“匿名bootstrap整段跳过”判断范围内。这可以解释部分Logo阶段为何不会触发定制跳过，但不能在缺少具体按键时刻时断定就是用户所见的阶段。游戏脚本与资源本轮不改。

音频选择保留当前audio7 DSP与CPyMO专属8抽头sinc前端配置。用户反馈前后都不会卡、耳机清晰无炸，扬声器仍有炸的感觉；没有同音量新旧耳机AB，不能声称本版听感胜出。此前相位节点修正有解析信号证据，普通音乐线性路径与CPU预算证据也成立，回滚会丢掉这些已证实修正。本轮不再改音效、压缩、音量或全局RetroArch设置。

读卡取得完整480000帧48kHz双声道PCM，前端accepted、partial_batches=0；4320次回调的单次最长混音59488us、提交66588us、BGM间隔70921us。单次最大耗时既不是平均CPU也不是ALSA underrun计数，不能据此宣布存在持续卡顿或完全没有欠载。
捕获PCM峰值左3335、右4465（满幅约10.2%、13.6%），无满幅或50%输出上限触顶样本。该采样早于RetroArch/ALSA，不能独自证明扬声器链路正常。采样累计最早10秒活跃BGM，跨播放事件时不保证单曲连续；分段可对齐梦见bgm09，增益约0.20。未记录切曲/重启时间，整段单曲SNR比较不成立，不将其用作音质评分。

本轮字体/布局与Logo回滚尚待真机复测。用户耳机反馈属于output8+375的既有结果，不冒充reading9+400的验收。
