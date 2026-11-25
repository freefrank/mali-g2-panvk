# Mali-G2 驱动开发交接（2026-11-25，迁移到 Windows + adb）

接手先读本文件、根目录 [AGENTS.md](../../AGENTS.md) 和 [driver/AGENTS.md](../../driver/AGENTS.md)。构建命令和回归测试的参数见 [BUILD_AND_TEST.md](BUILD_AND_TEST.md)，设备端脚本见 [../device/README.md](../device/README.md)。旧的交接和历史记录已移到 [archive/](archive/)，那些内容只作背景，不是操作清单。

Policy marker: biblioklept

## 1. 工作约束（用户明确要求）

- **只修驱动。** 目标只有 Mali-G2-Ultra-NX（Valhall v15，XRing O3）。上游 `src/panfrost` 对 AI 的限制不适用于这个 fork。
- **随时 commit push** 到 `origin/main`（`https://github.com/freefrank/mali-g2-panvk.git`）。不要 force-push，不要重写已推送的历史，不要向 Mesa/GitLab 提交或评论。提交标题优先用用户原话，正文带 `Generated-by: LLM` 和 `Policy-marker: biblioklept`。
- **性能是主线。不要再调在途数、组优先级、按帧限流这类参数**，直接攻 3 倍差距。
- 用户在玩游戏或反馈整机卡时，不要并行跑合成 GPU 测试。
- 诊断默认关闭，排查完要恢复。
- LOR 诊断 APK 不要卸载重装，否则会丢私有数据和存档。
- 测帧率必须同一天、同一位置先测基线，用 LOR 日志里的 `frame=` 算平均值，不要目测。同一个驱动在不同日子能差 40%。

## 2. 当前状态

| 项目 | 值 |
| --- | --- |
| 代码 HEAD | `main`，最后一个驱动改动是 `cbb43fc`（HSR 实验开关） |
| 推荐日常版 | test68 `flagsonly`（`647ee73`），也就是 test62 加上 v15 `Cull Zero Area` 和 `Disable VRS Clamp 2x2`。之后的 test69–74 默认行为相同，只是多了默认关闭的开关 |
| 最新包 | test74 `panvk-mali-g2-hsr-cbb43fc-b5eaa811.zip`（`tools/artifacts/test74/`），默认等同 test68 |
| 性能 | LOR 同场景我们约 15–25 FPS，原厂约 60 FPS |
| 设备属性 | `debug.mesa.panvk.g2.diagnostics=0`、`kbase.max.inflight=1`、`kbase.group.priority=-1`、`g2.vt.no.frag.wait=0`；残留的 `debug.mesa.panvk.debug=sync` 只在诊断开启时才生效 |

**进行中**：HSR 预处理的游戏内 A/B。开关是 `debug.mesa.panvk.g2.hsr`，0 为默认，1 在 v15 开启 HSR prepass，2 在 1 的基础上把深度刚好占一半预算的 Z/S tile 也当作可流水化。离线 MSAA 48/48 通过，深度、discard、blend 和局部渲染也都通过。设备端脚本是 `tools/device/abtest6.sh`，按 hsr0 → hsr2 → hsr0 跑，**还没在游戏里跑过**。

## 3. 3 倍差距目前的结论

**已经排除（不是主因）**
- 片元 shader 静态编译质量：和原厂（主 shader 加 pilot）相比是 1.03 倍。数据在 `fixtures/lor-pipeline-stats-compare.txt`。
- early-ZS 判定：没有 Force Late；LOR 的彩色 pass 本来就没有深度附件。
- render pass 固定开销：每帧多 0.4–2 ms。
- 驱动 CPU 开销：每帧多约 0.5 ms。
- 屏障里 tiler 等 fragment：游戏内只占 5–10%。
- BO 内存标志、提交结束时的刷新方式（`PANVK_KBASE_MEM_SET/CLR`、`FLUSH_MODE`）：合成测试里在 4% 以内，看不出差别。
- 片元 shader 用 48 寄存器档位、开 AFBC CRC：游戏里没有收益。

**仍然存在**
1. **单个 pass 慢约 1.5 倍。** 把 pass 串行跑来比，最差的是纯深度 pass（D32S8，阴影图一类，2.4–2.8 倍）和带深度的 1920×1308 pass（2.4–3.3 倍）。把最高频率压到 979 MHz，全屏 pass 的耗时几乎不变，说明是带宽或延迟受限。数据：`fixtures/lor-pass-timing-serial.txt`。
2. **GPU 空闲约 1.9 倍。** 在途数为 1 时，GPU 要等 CPU 录完下一次提交；LOR 在两次提交之间约 4.3 ms，governor 于是降频到 652 MHz。在途数改成 2 能多约 40%，但安卓界面会卡。降低组优先级、把调度周期缩到 20 ms 都没用，判断是 GPU 被我们占满，系统界面分不到资源。

## 4. 下一步

1. 跑 HSR A/B（`abtest6.sh`）。有收益就把默认值改掉。
2. 逐项查深度 pass 慢的原因：ZS 写回、AFBC ZS、tile 大小和 HSR。可以先用 `vulkan_tiles` 的 `MALI_G2_TILES_DEPTH_FORMAT=130` 加 `MALI_G2_TILES_TIMING=1` 复现。
3. LOR 每帧 3 次 `copy_image` 目标都是 AFBC，现在走 tiler 加全屏片元；原厂用纯片元的 frame shader，可以改成不经过 tiler 的路径。
4. 减少跨子队列同步：我们的 tiler 队列每帧约 25 次等待，原厂约 2 次。要先弄清 v15 `SHARED_SB_INC/DEC` 的语义。
5. 折中方案：用 `create_group` 的 `shader_core_count` 限制我们的核数，给系统界面留余量，以此缓解在途数 2 的界面卡。这和第 1 节说的“不再调参”有冲突，做之前先问用户。

## 5. 实验开关

Android 上的属性名由环境变量名转换而来：全部小写，`_` 换成 `.`，再加 `debug.mesa.` 前缀，例如 `PANVK_G2_HSR` → `debug.mesa.panvk.g2.hsr`。所有开关默认不改变行为。

| 开关 | 含义 |
| --- | --- |
| `PANVK_G2_DIAGNOSTICS` | 1 时记录事件日志到 `Download/panvk-g2-driver-events.log`（文件要预先建好，8 MiB 上限），每 5 s 输出一行 `submitstat`，每 2 万次绘制输出一次 early-ZS 直方图，并转储大 shader 的 NIR。这时 `PANVK_DEBUG` 才会被解析 |
| `PANVK_G2_HSR` | 0/1/2，见第 2 节 |
| `PANVK_G2_VT_NO_FRAG_WAIT` | 屏障处 tiler 不等 fragment。**不安全**，只用来测量 |
| `PANVK_KBASE_MAX_INFLIGHT` | 默认 1；-2 为按帧限流 |
| `PANVK_KBASE_GROUP_PRIORITY` | kbase 组优先级原值：0 高、1 中、2 低；-1 跟随 Vulkan |
| `PANVK_G2_TIB_SIZE` | 覆盖 tile buffer 大小（默认 32 KiB） |
| `PANVK_KBASE_MEM_SET/CLR`、`PANVK_KBASE_FLUSH_MODE` | BO 内存标志、提交结束时的缓存刷新模式 |
| `PANVK_G2_TILER_INITIAL_CHUNKS/MAX_CHUNKS`、`PANVK_G2_EXPERIMENTAL` | tiler heap 大小；关闭实验性特性 |

## 6. 迁移到 Windows + adb

### 6.1 什么在 Git 里，什么不在

Git 里有：驱动源码、`tools/` 下的源码和脚本、`tools/device/` 设备端脚本、`tools/fixtures/` 测试输入和结果摘要、文档。

**只在本机（手机的 PRoot 里）、clone 拿不到的：**

| 路径 | 大小 | 迁不迁 |
| --- | --- | --- |
| `tools/artifacts/` | 161 MB | **要迁**。test45–74 的 ZIP 和 `symbols/*.zst`，tombstone 符号化离不开它们 |
| `tools/work/` | 218 MB | **要迁**。`vendor-snap/`（原厂命令流快照、解码工具、`compare.md`）、`vsdump/` 和 `fsdump/`（LOR shader NIR）、各轮 A/B 原始数据 |
| `tools/build/bin/` | 187 MB（整个 `build/`） | 建议迁。Android 版辅助程序（`vulkan_tiles`、`counter_percore`、`replay_nir_android` 等）可以直接推到设备上用，换了工具链之后也能重建 |
| `references/malibridge/`、`references/eden-upstream/` | 22 MB | 可迁，也可以重新获取（来源见 `references/SOURCES.md`） |
| `toolchain/`、`driver/build-*` | 1.2 GB | **不迁**。这些依赖 Termux clang 和 PRoot 路径，在 Windows 上没用，要重建 |
| `/root/lor/LostOdysseyRecomp` | — | 不用迁。诊断分支 `diag/pipeline-stats`（`9850419`）已推到 GitHub |

在 PRoot 里打包到公共存储，再从 PC 拉取：

```sh
cd /root/mali-g2-project
tar -cf /storage/emulated/0/Download/mali-g2-local.tar \
  tools/artifacts tools/work tools/build/bin references/malibridge references/eden-upstream
# PC 上：
adb pull /storage/emulated/0/Download/mali-g2-local.tar
```

解包到 clone 下来的仓库根目录。之后可以删掉 Download 里那份。

### 6.2 构建环境

建议用 **WSL2 Ubuntu** 构建，adb 用 Windows 端的 platform-tools；WSL 里也可以直接调用 `adb.exe`。原生 Windows 不行：第一步的 host 工具（`mesa_clc`、`vtn_bindgen2`、`panfrost_compile`）需要 LLVM、libclc 和 SPIR-V LLVM Translator，`driver/build.sh` 已经写明只支持 Linux 和 WSL。

1. 装依赖：meson、ninja、python3（mako、packaging、pyyaml）、pkg-config、glslangValidator，以及**同一主版本**的 llvm-dev、libclang-dev、libclc-XX-dev、libllvmspirvlib-XX-dev 和 clang。SPIRV-Tools 要比 2023.6 新；旧环境是自己编译后用 `PKG_CONFIG_PATH` 指过去的。
2. 下载 Android NDK r29，然后运行 `driver/build.sh <NDK 路径>`。它会依次构建 `build-host`，生成 gralloc 桩，再交叉编译 `build-android`（API 31，arm64）。旧环境里手写的 `toolchain/android-cross.ini` 和 Termux clang 的包装脚本都不再需要。
3. 构建完必须检查：`llvm-readelf -d libvulkan_panfrost.so` 的 NEEDED 列表里**不能有 `libc++.so`**，否则应用一创建设备就闪退（test8 出过这个问题）。
4. 改了 `src/panfrost/compiler` 里的结构体以后，要先重编 host 的 `panfrost_compile`，再删掉 `libpan_shaders_v*` 重新生成。
5. 打包：`python3 driver/android/package.py <stripped.so> <输出目录> <标签>`。标签必须是新的，否则应用不会替换已装的驱动。
6. 第一次在新环境出包时，先用同一个提交构建，和 `tools/artifacts/test74` 里的库对比；行为一致之后再开始改代码。

### 6.3 待适配（迁移后先做）

- `tools/build_helpers.py` 第 57 行写死了 `/data/data/com.termux/files/usr/bin/clang`，要改成 NDK 的 `aarch64-linux-android31-clang`。
- `test_android.py`、`test_msaa.py`、`test_eden_compute.py` 和 `fixtures/sweep*.sh` 默认在手机本机执行测试程序（`LD_LIBRARY_PATH=/vendor/lib64...`，驱动路径 `/vendor/lib64/hw/vulkan.mali.so`）。要加一层 adb 封装：把程序、驱动和 SPIR-V 推到 `/data/local/tmp/mali-g2/`，再用 `adb shell` 执行，然后把输出拉回来。
- **未验证**：`shell` 用户能不能直接打开 `/dev/mali0` 跑 Vulkan 测试。不能的话改用 `adb shell su -c`。
- 仓库已加 `.gitattributes`，`*.sh` 强制 LF。Windows 端不要用会把换行改成 CRLF 的编辑器保存设备脚本。

### 6.4 设备操作对照

| 以前（PRoot + TermuxService） | 现在（adb） |
| --- | --- |
| `tools/work/takeover/run.sh x.sh marker` | `tools/device/adb-run.sh x.sh [--bg]` |
| 直接读写 `/storage/emulated/0/...` | `adb pull` / `adb push` |
| 读 LOR 私有目录要 `nsenter -t 1 -m --` | `adb shell su -c 'cp ... /storage/emulated/0/Download/...'`，再 `adb pull` |
| `pm install` 报 binder 错误 | `adb install -r <apk>`，签名冲突时**不要卸载** |
| `/system/bin/getprop`、`setprop` | `adb shell getprop`、`adb shell su -c 'setprop ...'` |
| 本地 `llvm-symbolizer` | NDK 里的 `llvm-symbolizer`，配合 `tools/artifacts/symbols/` |

- KernelSU 管理器里要给 `com.android.shell` 授予 root。
- LOR 日志在 `/storage/emulated/0/Android/data/io.github.freefrank.lostodyssey/files/logs/runtime-*.log`；驱动在应用私有目录的 `files/gpu_driver`。
- GPU 频率：`/sys/class/devfreq/gpufreq_core/cur_freq`；kbase 调度周期：`/sys/class/misc/mali0/device/csg_scheduling_period`（默认 100 ms）。

## 7. 文档地图

| 文件 | 内容 |
| --- | --- |
| 本文件 | 当前状态、下一步、迁移 |
| [BUILD_AND_TEST.md](BUILD_AND_TEST.md) | 构建，以及各个合成回归测试的参数 |
| [DATA_LAYOUT.md](DATA_LAYOUT.md) | 目录职责和发布规则 |
| [../device/README.md](../device/README.md) | 设备端脚本 |
| [../continuation.json](../continuation.json) | 机器可读的当前状态 |
| [archive/HANDOFF-2026-11-10.md](archive/HANDOFF-2026-11-10.md) | 10-30 到 11-10 的逐日实验记录（LOR 性能排查、test46 Eden 退出分析） |
| [archive/STATUS-2026-10.md](archive/STATUS-2026-10.md) | test10 时期的状态和更早的快照 |
