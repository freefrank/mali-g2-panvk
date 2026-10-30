# Mali-G2 驱动开发交接（当前 test46）

先读本文件、根目录 [AGENTS.md](../../AGENTS.md) 和 [driver/AGENTS.md](../../driver/AGENTS.md)，再按需要读 [构建与测试](BUILD_AND_TEST.md)。[STATUS.md](STATUS.md) 保存历史快照，不是当前操作清单。

**用户最新范围：只修驱动。当前 test46 已应用，但游戏切回退出与持续卡顿都尚未解决。** 已有一项 ANB 内存回收容错修复通过定向对照，不能据此宣称所有闪退已修复。

Policy marker: biblioklept

## 1. 给接手 agent 的任务

在 `/root/mali-g2-project` 继续 Mali-G2 PanVK/kbase 的正确性与性能开发，保留已有文字、3D、开场渲染修复和 test45/test46 的有效增量。目标仍是游戏在 1× 下流畅运行，用户期望 60 FPS；现有持续卡顿与部分切回退出未解决。

用户原话和工作约束：

- 「只修驱动」：可以读取 Eden 日志和参考源码；没有授权修改、补丁注入、重建或替换 Eden APK/原生代码。不要反复询问已经明确的范围。
- 「把驱动中的诊断都关了，性能很差，出问题再开」：诊断默认关闭；有明确排错需要才临时开启，结束后恢复关闭。
- 「记得随时commit push」：独立阶段及时提交并推送个人仓库 `origin/main`；不 force-push、不重写已推送历史、不向 Mesa/GitLab 上游提交或评论。遵守 AI 标记要求，提交标题优先采用用户原话，保留 `Generated-by: LLM`。
- 先看本轮 Eden 日志、退出记录和对应 tombstone，再判断闪退；不要跳过模拟器日志直接猜 GPU 根因。
- 用户游戏运行或报告整机卡慢时，不并行运行合成 GPU 回归。只读观察采样与主动测试负载必须分开。
- 修改应用配置/替换驱动前确认没有运行中的游戏；先备份，保护存档。不能把原先的退出确认当成后来新会话的退出确认。
- 最新反馈覆盖短时成功：test46 先报告“切回正常”，随后又退出，当前结论仍是未解决。

本次用户要求写交接文档，未启动新的 GPU 测试或改动设备；下一轮开发由接手 agent 继续。

## 2. 代码、版本与文件位置

这是一个 Git 仓库，`driver/` 是子树，不是应另行推送的旧仓库。远端为 `https://github.com/freefrank/mali-g2-panvk.git`，分支 `main`。

| 项目 | 当前基线 |
| --- | --- |
| 本次写文档前的仓库检查点 | `8277b4956d10ae11d563a775034f7e6775e3eb35`；文档提交会更新 HEAD |
| test46 驱动源码提交 | `49bf70ee39f7bb29ea5be30e7ed6cb9633e0aeb3` |
| test46 的 driver 子树 | `df8ccad5ef2f85c4f0d0ce23ebe00442e9641c35` |
| test45 源码对照 | `24e32e8b86164c9375aebab92a037b487f8ed872` |
| test46 元数据 | [test46-build.json](../fixtures/test46-build.json) |
| 机器可读续接状态 | [continuation.json](../continuation.json) |
| 下一驱动试版编号 | `test47`；`eden-perf48/49` 是采样目录，不是驱动版本 |

工作区路径：`driver/build-host`、`driver/build-android-arm`、`toolchain/` 为现有构建环境；`tools/build/` 为辅助程序；`tools/artifacts/` 为交付物/符号；`tools/work/` 为原始日志、脚本与临时结果。

`tools/artifacts/`、`tools/work/`、构建目录及部分外部参考被 Git 忽略。**同一工作区的接手 agent 可以直接使用；仅 clone Git 不会得到库、ZIP、符号、原始设备日志或完整工具链。** Git 中保留脱敏摘要及文件哈希，不要把原始日志、私有配置、配对码和密钥推送出去。

## 3. 当前驱动包与设置

包文件：

```text
/storage/emulated/0/Download/panvk-mali-g2-test46-resume-49bf70e-0f5c62e4.zip
/root/mali-g2-project/tools/artifacts/test46/panvk-mali-g2-test46-resume-49bf70e-0f5c62e4.zip
```

| 内容 | SHA-256 |
| --- | --- |
| test46 库 | `0f5c62e45a6135d68c91c287e51e970276514056a29a2d7a0fb6fdbe0a0a00b2` |
| test46 ZIP | `600a55340c99ae9b05e9359fc259bdf462a2dce2a7f93bea94a9471f8ec67497` |
| test45 库对照 | `7d407064272ac63efa11d8958273da6d8e8cffc327b7bdf6cf8ff5efc827450e` |

未剥离库：`tools/work/eden-test46/test46-unstripped.so`。压缩符号：`tools/artifacts/symbols/test46-unstripped.so.zst`。完整哈希见 manifest，不要用 test45 符号解释 test46 地址。

最后核验：实际运行 PID 12997 的 `/proc/PID/map_files` SHA 与 test46 一致；之后又经原生 PID 20151 的 `/proc/PID/root` 文件视图核对相同 SHA。PID 和无线调试端口都是历史值，接手时重新读取。

| 配置 | 当前值 |
| --- | --- |
| 分辨率 | 1×，`resolution_setup=3` |
| 呈现模式 | Mailbox，`use_vsync=1` |
| 异步 GPU | `use_asynchronous_gpu_emulation=false` |
| 异步呈现 | `async_presentation=false`，与异步 GPU 是不同开关 |
| Eden 验证/GPU 日志/转储 | 关闭 |
| 驱动诊断 | 关闭；属性为空也使用默认 false |
| GPU 调度 tick | 恢复为 100ms |
| 系统窗口模糊 | 恢复原值：`disable_window_blurs=null`，Blur enabled=true |

test46 应用时只改全局 `driver_path` 一行，并更新库、meta 与选择的 ZIP；游戏自定义配置逐字节未变。详见 [应用记录](../fixtures/test46-applied.json)。该报告的 `game_validation=pending` 是应用时点快照，后续失败以其他证据为准。

上轮备份路径：

```text
/data/data/com.miHoYo.Yuanshen.nightly/files/panvk-backups/test45-before-test46-20261030T122924Z
```

内有 test45 库、meta、全局及游戏配置。需要回退时先确认当前用户状态与挂载视图，不要自动用历史整份配置覆盖用户后来的修改。

## 4. test46 改动与验证边界

源码入口：

- [panvk_image.c](../../driver/src/panfrost/vulkan/panvk_image.c)：`panvk_DestroyImage` 仅在 Android、kbase、架构 15 且 `image->vk.anb_memory` 非空时等待，再释放图像隐式持有的 ANB 内存。
- [kbase_kmod.c](../../driver/src/panfrost/lib/kmod/kbase_kmod.c)：`pan_kmod_kbase_csf_wait_idle` 在完成锁下逐组读取 `next_seq-1`，通过条件变量等待 `done_seq` 到达该水位；`done_seq` 在原完成回调之后推进。
- [kbase_kmod.h](../../driver/src/panfrost/lib/kmod/kbase_kmod.h)：声明等待接口。
- [vulkan_android.c](../vulkan_android.c)：增加 `MALI_G2_ANB_RETIRE_PENDING` 容错回归。

普通图像、应用显式分配的外部 AHB 内存与正常每帧提交/呈现路径没有新增等待。保留 test45 的 G2 计算/片元仲裁 threshold=4、ratio=3，原队列 Priority=2、核心掩码及缓存/fence 修复保持。

**这是对已经观测到的提前销毁 ANB 映射的保护，不是 Vulkan 合规性证明，也不是完整应用生命周期修复。** 等待范围是已进入 kbase 组的工作；它没有调用通用 `DeviceWaitIdle` 去排空所有潜在的运行时待提交任务。后续审查应关注组生命周期、并发提交/销毁、等待活性与故障处理，不能把尚未验证的边界当作已经覆盖。

最终 stripped 包运行验证：29 项候选用例 + 1 项 test45 预期故障对照，共 30 项通过。其中候选包含 ANB 提前销毁 1、Android 导入/绘制 9、提交/完成/native fence/MSAA/颜色/压缩纹理/深度 19。

- 定向对照：test45 销毁约 0.070ms 返回时 fence 仍 pending，随后 `VK_ERROR_DEVICE_LOST`；test46 等待约 139.821ms，返回时 fence 已完成、流程成功。
- 这是故意提前销毁资源的容错用例，不能当成合法 Vulkan 应用用法，也不能把 140ms 解读为每帧开销。
- 另验 27 个 `SHF_ALLOC` 节的 stripped/unstripped 内容及布局一致、ZIP CRC/内部库 SHA、Download 副本 SHA。
- 每个主动 GPU 用例前均用原生线程名检查确认没有 Eden 游戏负载。
- 没有取得 test46 在游戏中的稳定 60 FPS 或持久无崩溃证明。

报告：[运行回归](../fixtures/test46-runtime.json)、[文件一致性](../fixtures/test46-packaged-integrity.json)。本地执行脚本 `tools/work/eden-test46/run-validation.py` 固定写 test46 路径、旧 source hash 和 adb serial；新版本必须适配，不能照跑覆盖旧证据。

## 5. 退出现象必须分开判断

时间均为 UTC，原生 tombstone 常显示 `+0800`。

| 事件 | 已确认事实 | 结论与证据 |
| --- | --- | --- |
| 早期返回游戏列表 | 旧进程/游戏线程仍活着，重复 MainActivity 盖住 EmulationActivity | 清理任务并改用标准 launcher intent；仅代表当时任务入口修正。[任务证据](../fixtures/test45-task-resume.json) |
| test45 GPU 页错误 | PID 4234：ANB BO `0x722bc00000`，大小 `0x1d8a000`，在 boot 439423.784365 释放；439427.900551 GPU WRITE 到 `0x722cc7e7c0` | 3,639/3,639 条事件，零丢失；故障地址在已释放 BO 内。促成 test46 容错修复。[生命周期证据](../fixtures/test45-resume-device-loss.json) |
| test46 12:31:03 | PID 12997，GPU 线程 SIGABRT，未捕获 `VK_ERROR_INITIALIZATION_FAILED`；Eden 末条错误为 CreateSurface 失败 | 与 GPU DEVICE_LOST 不同；本轮未观测新的 Eden GPU 页错误。[异常证据](../fixtures/test46-game-resume.json) |
| test46 12:36:20 | PID 15848，GPU 线程 SIGSEGV；系统 Vulkan loader `+0x3326c` 执行 `ldr x8,[x25]`，x25=0，swapchain 参数 x1=0 | Eden 向系统取图接口传入空交换链，尚未进入 PanVK 的 AcquireImageANDROID。[最终捕获](../fixtures/test46-surface-capture.json) |

最后一次表面记录共 24/24 事件、8 次 CreateSurface，函数指针全部非空、实际 VkResult 全为成功，包括切回后的两次。因此不能把这个窗口的退出继续说成“表面创建失败”。

参考源码与准确 APK 反汇编显示，`Swapchain::Create` 在 `maxImageExtent` 为 0 时可直接返回，`AcquireNextImage` 没有空交换链检查。**“零尺寸导致此次空句柄”仍是源码支持的解释，本次未直接捕获该次 capabilities 值。** 空句柄和 loader 处的空读则有寄存器与指令直接证据。

标准 ICD 接口不能直接处理尚未到达 ICD 的非法句柄。这个事实不自动排除之前的驱动行为影响重建时序；若继续追因必须补充相应证据，并遵守用户只修驱动的约束。不要为了绕开该崩溃偷偷改 APK、注入 loader 或无依据地串行化每一帧。

易误判点：tombstone 可能残留 `host_memory.cpp:770` assert 的 Abort message，不能覆盖实际信号/PC；内核最新页错误可能来自旧库主动对照 `vulkan_android` PID 17620，不能归给 Eden PID 12997。

## 6. 持续卡顿的已有结论

用户在同一室内曾约 35–45 FPS，战斗约 50 FPS；1× 希望 60 FPS。0.5× 基本 60 FPS，属于诊断对照，不是目标达成。test41 约 55–58 FPS 仍持续抖动，test42 明显改善后仍卡；test45 初期“好了很多”，数秒后游戏和通知栏仍卡。test46 没有新的可靠 FPS 对照。

test44 的联合采样曾把等待链连到 SystemUI dequeue → SurfaceFlinger presentDisplay → HWC 等上一帧 → DRM 提交 → SurfaceFlinger GPU fence。对应通知栏操作窗口，显示提交等待 SF GPU 时间线 p95 约 75.42ms、最大 99.90ms，游戏 GPU 时间线 p95 约 30.09ms；说明该窗口 SF GPU 信号迟到，但仍需区分它自身工作、依赖和调度。

test45 的另一 20 秒窗口中 GPU 利用率 91–99%，大部分 1.248GHz，GPU 温度 45–46.875°C，Thermal Status=0；没有捕获经确认的“顺滑转卡顿”边界，不能全面排除温控/频率策略，也不能与不同通知栏操作窗口直接算性能提升。

已经试过或恢复的状态：

- FIFO → Mailbox：用户反馈卡顿和 FPS 基本相同；当前仍 Mailbox。
- GPU 调度 tick 100 → 10ms 临时对照：用户仍卡，已恢复 100ms。
- 窗口模糊临时关闭：已自动恢复；补采缺少新增 SystemUI 帧，不能当有效操作对照。
- 不并行 GPU 测试时系统仍卡；不能把所有卡顿都归因于诊断程序。
- 120Hz/低 GPU 利用率的某些采样实际是 Termux 前台，已排除；几次等待采集超时没有取得有效游戏窗口。

证据：[系统合成与 fence](../fixtures/test44-system-composition-profile.json)、[test45 资源/退出调查](../fixtures/test45-resume-investigation.json)。更早试验、test40 撤回等细节查 [STATUS.md](STATUS.md)。

## 7. Android 访问：先核对通道与视图

设备 Xiaomi M367FC / `yingtian`，XRing O3，Mali-G2-Ultra-NX MC16、Valhall v15；Android API 37、已 root。本机是 Termux + proot ARM64 环境，PRoot 的“root”不等于 Android 原生 root。

包名 `com.miHoYo.Yuanshen.nightly`，游戏单独配置 ID `0100000011D90000`。当前精确 APK 的 libyuzu Build ID 为 `7f8a58a085a4c1ab144e470d4941f1857b2f7a81`；后续若用户更新应用，所有偏移需重新核对。

adb 已安装 `android-tools 34.0.4-3`，曾配对连接 `127.0.0.1:33641` 并获得 uid=0，最后已断开。Shizuku 服务曾存在，本轮实际使用 adb 与原生 root 桥。

```sh
adb devices -l
adb connect 127.0.0.1:33641
```

这个 serial 只是上次端口，不能假定仍有效；5555 当时未监听。先查看现有连接/原生 adbd 监听信息，确需重新配对才向用户询问临时配对信息，不记录密钥或配对码。

### 原生 TermuxService root 备用通道

同一工作区已保留只读检查脚本 `tools/work/eden-perf49/check-cleanup.sh`。下面命令让原生 Android sh 执行该脚本，再由其调用 `/system/bin/su -c`：

```sh
timeout 10 env -u LD_PRELOAD -u LD_LIBRARY_PATH \
  /data/data/com.termux/files/usr/bin/bash \
  /data/data/com.termux/files/usr/bin/am startservice --user 0 \
  -n com.termux/com.termux.app.TermuxService \
  -a com.termux.service.action.service_execute \
  -d file:///system/bin/sh \
  --esa com.termux.execute.arguments \
  /data/data/com.termux/files/usr/var/lib/proot-distro/containers/base/rootfs/root/mali-g2-project/tools/work/eden-perf49/check-cleanup.sh \
  --ez com.termux.execute.background true
```

输出目录 `/storage/emulated/0/Download/panvk-perf49-cleanup/`。重点读取 `app-pid.txt`、`identity.txt`、`process-view-driver-sha.txt`、`probes.txt`、`instances.txt`、`diagnostics.txt`；原始输出保持本地。

该脚本对直接私有路径的检查可能报 ENOENT，但对进程视图的检查仍成功。root 会话的挂载视图曾隐藏 `/data/data/包名`；这不等于驱动删除或应用卸载。**使用原生实时 PID 的 `/proc/PID/root`，不要硬编码 20151。** 若游戏完全退出而无 PID，这个进程视图不存在，应换用可见该目录的原生挂载视图，不能凭空拼旧 PID。

在原生 root shell 中可按以下方式只读核对：

```sh
package=com.miHoYo.Yuanshen.nightly
app=$(pidof "$package")
[ -n "$app" ] || exit 1
sha256sum "/proc/$app/root/data/data/$package/files/gpu_driver/libvulkan_panfrost.so"
for f in /proc/$app/task/*/comm; do
  cat "$f"
done
```

线程名必须读 `/proc/PID/task/TID/comm`；本机 `ps -T -o NAME` 输出进程名，不能据此说没有 GPU 线程。`GPU`、`VulkanWorker`、`VulkanPresent`、`GPUFencingThrea`、`CPUCore_*`、`NativeEmulation` 都是要检查的状态线索。

原生 `cmd/settings` 输出直接重定向到共享存储可能 Failed transaction，可尝试 `</dev/null 2>&1 | cat > 文件`；仍须检查正文并读回结果，不能仅凭 `su_exit=0` 判成功。

## 8. 日志、原始证据与符号

| 内容 | 位置/注意 |
| --- | --- |
| 全局配置 | `/storage/emulated/0/Android/data/包名/files/config/config.ini` |
| 游戏配置 | 同目录 `custom/0100000011D90000.ini` |
| Eden 当前日志 | `/storage/emulated/0/Android/data/包名/files/log/eden_log.txt` |
| 上轮日志 | `eden_log.txt.old.txt`，不是 `.old`；启动会轮转 |
| GPU 详细旧日志 | `eden_gpu.log` 可能约 2GB，不要整份复制或读取 |
| 进程退出记录 | 原生 `dumpsys activity exit-info 包名` |
| 原生崩溃 | `/data/tombstones/tombstone_NN`；按 PID/时间选本轮，不只按编号猜 |
| 实际库映射 | 原生 `/proc/PID/maps` 与 `/proc/PID/map_files/范围` 的 SHA |
| 生命周期证据 | `tools/work/eden-perf48/{initial,repro,lifetime}/` |
| test46 首次应用/新退出 | `tools/work/eden-test46/{applied,game-validation,new-exit}/` |
| 最新空句柄/表面记录 | `tools/work/eden-perf49/{surface,repro,final-state}/` |
| 精确 APK 提取库 | `tools/work/eden-perf46/app-symbols/libyuzu-android.so` |
| 只读参考源码 | `references/eden-upstream/src/src/video_core/`；版本可能不同于安装 APK |

完整包/符号/原始文件哈希由各 `fixtures/*.json` 指向。Eden 完整栈中的 loader 是 memfd 拷贝，分析时曾对照本机 `/system/lib64/libvulkan.so` 的实际指令；不要直接拿 Mesa 符号解释 Android loader PC。

test46 未剥离库可用于 `llvm-symbolizer -e tools/work/eden-test46/test46-unstripped.so 0x地址`。simpleperf 的库内“相对可执行映射偏移”可能需要换算为 ELF 虚拟地址，不能直接当 PC 使用；各版本 LOAD 偏移以本 ELF 为准。

## 9. 接下来怎样推进驱动

1. 读取本文件和机器状态，确认工作区、分支、实际驱动 SHA、应用版本与用户是否正在游戏；不要重新导入旧包或重复启用诊断。
2. 区分当前问题是新的 GPU fault/DEVICE_LOST、系统 loader/应用非法句柄，还是同场景帧时间抖动。对新退出先取日志和本轮 tombstone。
3. 继续性能工作时，采同一室内位置、实际游戏前台的帧时间、GPU 状态和合成 fence，尽量覆盖“初期顺滑→变卡”边界；说明观察性采样本身的限制。已有包没有证明 1× 持久 60 FPS。
4. 审查 test46 等待范围与已有提交/完成路径，可以据驱动证据完善；不要声称已排空所有应用层待提交工作，也不要无依据扩大为每帧同步。
5. 新修复先记录独立源码检查点并 push，再构建、做相关且必要的回归、保存符号与脱敏报告、校验包与实际加载 SHA。没有新变更或新疑点时不要反复扩大测试。
6. 碰到只在系统空句柄路径退出的记录，如实保留边界；用户限定只修驱动，不得借“兼容”之名更改 Eden 或注入系统 loader。此约束不妨碍继续有证据的驱动性能/正确性研究。

### 构建入口

```sh
env LC_ALL=C \
  PATH=/root/mali-g2-project/driver/build-host/bin:/root/mali-g2-project/toolchain/bin:/usr/bin:/bin:/usr/sbin:/sbin \
  ninja -C /root/mali-g2-project/driver/build-android-arm -j3 \
  src/panfrost/vulkan/libvulkan_panfrost.so
```

已有 host/Android 构建可增量使用。辅助程序的 Android clang 必须带 `-no-canonical-prefixes`；工具链、gralloc 桩、C++ 静态运行时与 NIR 元数据重建细节见 [BUILD_AND_TEST.md](BUILD_AND_TEST.md)。不要引入设备无法加载的 `libc++.so` 依赖。

确认游戏完全退出且通过原生线程检查后，定向容错回归入口为：

```sh
cd /root/mali-g2-project/tools
env PANVK_G2_DIAGNOSTICS=0 \
  MALI_G2_ANB_RETIRE_PENDING=1 \
  LD_LIBRARY_PATH=/vendor/lib64:/vendor/lib64/egl:/system/lib64 \
  build/bin/vulkan_android /path/to/candidate/libvulkan_panfrost.so
```

原用例故意提前销毁资源，旧库预期失败。新版本测试不要覆盖 test46 归档输出。打包应使用新的版本名/源码 SHA/库 SHA，保留旧包与符号。文档提交后的仓库 HEAD 会不同于已交付包的源码提交；以 manifest 和 driver 子树核对，不能只比较 HEAD 字符串。

## 10. 可直接给下一位 agent 的提示

```text
在 /root/mali-g2-project 接手 Mali-G2 PanVK/kbase 开发。
先读 AGENTS.md、driver/AGENTS.md、tools/docs/HANDOFF.md 和 tools/continuation.json。
当前包 test46，源码 49bf70e，库 SHA 前缀 0f5c62e4；GPU 提前释放 ANB 的容错回归已通过，游戏切回和持续卡顿仍未解决。
用户只允许修驱动，诊断默认关闭，阶段性 commit/push 到 origin/main。
Android 已 root；adb 上次断开，可用文档中的 TermuxService 原生 root 桥；私有目录可能需经实时 /proc/PID/root 访问。
先依据本轮日志和证据继续，不改 Eden、不把历史待验证项当作当前结论；游戏运行时不并行 GPU 合成测试。
```
