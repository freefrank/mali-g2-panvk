# 设备端脚本

这些是 Android 原生 shell 脚本（`#!/system/bin/sh`），在手机上执行，内部用 `/system/bin/su -c` 提权。输出写到 `/storage/emulated/0/Download/panvk-*/`。原先放在被忽略的 `tools/work/takeover/`，靠 PRoot 里的 TermuxService 启动；迁移到 Windows + adb 后统一放这里并纳入 Git。

- 运行：`tools/device/adb-run.sh <脚本> [--bg]`。会等用户开游戏的 A/B 脚本（`abtest*.sh`、`lorperf.sh`）用 `--bg`。
- 取结果：`adb pull /storage/emulated/0/Download/panvk-ab6 tools/work/`。
- 脚本要用的设备端辅助程序（如 `counter_percore`）放在 `/data/local/tmp/mali-g2/bin/`：`adb push tools/build/bin/. /data/local/tmp/mali-g2/bin/`，再 `adb shell chmod 755 /data/local/tmp/mali-g2/bin/*`。
- KernelSU 管理器里要给 `com.android.shell` 授予 root，否则 `su` 会失败。

常用脚本：

| 脚本 | 用途 |
| --- | --- |
| `abtest.sh`–`abtest6.sh` | LOR 游戏内 A/B：每轮跟踪一个新的 LOR 进程，等日志出现 `map=` 后再计时，在两轮之间切换属性。`abtest6.sh` 是 HSR 0/2/0，还没跑过 |
| `lorperf.sh`、`lorvendor.sh` | LOR 计数器、SurfaceFlinger timestats、GPU 频率（我们的驱动 / 原厂驱动） |
| `freqsample.sh`、`gpupin.sh` | GPU 频率采样，尝试锁频（锁频大约 31 秒后会被系统改回） |
| `props.sh`、`propoff.sh` | 读出 GPU 相关属性；把诊断属性复位为关闭 |
| `state.sh`、`loaded.sh` | Eden（`com.miHoYo.Yuanshen.nightly`）的进程、线程和已加载驱动 |
| `sffps2.sh`、`jankprobe*.sh`、`schedprobe*.sh` | SF 帧率、界面卡顿、调度采样 |
| `shaderdiag*.sh`、`fsdiag.sh`、`pstats.sh` | 开诊断跑一次 LOR，再把属性恢复原样 |
| `lo*.sh`、`lslogs.sh`、`cplogs.sh`、`lormaps.sh` | 拷贝 LOR 日志、配置和 maps |
| `tomb.sh`、`klog.sh`、`dm.sh` | tombstone、kmsg 和 dmesg |
| `getcap.sh`、`findcap.sh` | 导出 LOR 帧捕获（约 1 GB） |
| `gpudir.sh`、`procdrv.sh` | 查看 LOR 的 `files/gpu_driver` 和设置；核对进程实际加载的驱动 |
| `install.sh` | 在设备上用 `pm install-create` 装 LOR 诊断 APK；这台机器上报 binder 错误。现在直接用 PC 端的 `adb install -r`，签名不同时**不要卸载**（会丢存档） |

其余是一次性的排查脚本，留着作参考。改哪个脚本之前先读一遍：不少脚本硬编码了包名、输出目录和属性名。

Policy marker: biblioklept
