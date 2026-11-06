# v0.24.5 启动器修复验收

本轮仅修改 `deploy/Roms/APPS/CPyMO.sh`，不改 C 核心。以设备实际 `mktemp: Input/output error` / `refused_private_config` 为反例，移除 mktemp 依赖，使用原子 mkdir、0700目录/0600私有配置及内存文件系统回退。保留原来的独立音量解析、配置优先级、50%增益和退出/信号清理。

在新的可写Linux QA根目录准备 `repo/`、`evidence/CPyMO_device.sh`（原卡旧启动器）和 `evidence/retroarch-1.14.0/`（原v0.24.3a工程里的未改上游解析器源码/头文件/SOURCES.json）。依赖cc、Python3、dash、BusyBox；不需重编译核心或接入TF卡：

```sh
sh repo/tests/v0243a/build_ra_probe.sh
python3 repo/tests/v0245_launchfix/test_runtime.py
python3 repo/tests/v0245_launchfix/test_volume_regression.py
python3 repo/tests/v0245_launchfix/test_edges.py
```

实际结果：32项运行目录/故障/重名/并发/挂载前缀场景，108项音量与退出信号回归，6项边缘/真实配置解析器优先级，共146项断言场景通过。包含按预期退出的旧版反例与全部内存候选不可用的反例，不代表146次都应成功启动。

运行目录故障通过给私有脚本副本增加同名shell函数注入。BusyBox ash可能优先使用内置applet，因此不能只放一个PATH中的坏mktemp就宣称覆盖该故障；注入函数在dash与ash中都能生效。被测生产函数体不改。挂载前缀测试仅把被测函数里的/proc/mounts重定向到测试表，验证最长前缀、边界和转义空格，避免误用TF文件系统。

测试中的RetroArch替身使用真实1.14.0配置解析器核查有效音量/参数，不运行音频设备或游戏。所有资源与旧档均为私有假文件；检查hash和mtime不变、独立临时目录及清理。已有目标文件/符号链接的冲突不得重用或删除。CPYMO_RUNTIME_ROOT只用于QA显式临时根，实际掌机默认不设置；自动候选只接受tmpfs/ramfs/rootfs/devtmpfs。

通过这些测试表示已验证软件修复及回归，不证明设备的底层I/O错误完全消失。若新日志仍退出，按runtime_failure和runtime_fs继续定位，不通过清空存档或整体覆盖目录处理。
