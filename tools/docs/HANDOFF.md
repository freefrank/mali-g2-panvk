# 交接文档（2026-10-29，test28 之后）

接手前先读完本文件，再读 `STATUS.md`（历史结论）和 `BUILD_AND_TEST.md`（构建细节）。
Policy marker: biblioklept

## 1. 工作环境

| 项 | 值 |
| --- | --- |
| 设备 | Xiaomi M367FC（yingtian），SoC 玄戒 XRing O3 |
| GPU | Mali-G2-Ultra-NX MC16，Valhall v15（PAN_ARCH 15），`/dev/mali0` kbase 1.39 CSF |
| 原厂驱动 | `/vendor/lib64/hw/vulkan.mali.so`，Vulkan 1.4.345，可作对照 |
| 系统 | Android；Termux 内跑 proot Manjaro ARM64（`/root` 在 proot 里） |
| 权限 | Termux 有 root：proot 内 `su -c '...'` 可用；Android 系统命令需经 `/system/bin/sh -c "su -c '/system/bin/<cmd> ...'"`，例如 `setprop`/`getprop` |
| 包管理 | `pacman` / `yay` 可装依赖 |
| 仓库 | `/root/mali-g2-project`（单仓库，`main`），远端 https://github.com/freefrank/mali-g2-panvk （public） |
| 目标 | 只适配玄戒 O3 / Mali-G2 v15，不考虑其他设备兼容性 |
| Eden | 包名 `com.miHoYo.Yuanshen.nightly`（Eden 改名版），日志目录见第 4 节 |

约定（`driver/AGENTS.md`）：提交信息必须带 `Generated-by: LLM`，文档带 `biblioklept` 标记；禁止在 Mesa GitLab 发帖/评论/开 MR。上游 AGENTS.md 对 `src/panfrost` 的 AI 禁令已移除，本 fork 可改。

git 若报 dubious ownership：已设 `safe.directory '*'`；proot 中用 sudo 后需 `chown -R 10312` 回 Termux UID。

## 2. 构建与发布

```sh
# Android 驱动（增量）
env LC_ALL=C PATH=/root/mali-g2-project/driver/build-host/bin:/root/mali-g2-project/toolchain/bin:/usr/bin:/bin:/usr/sbin:/sbin \
  ninja -C /root/mali-g2-project/driver/build-android-arm -j3 src/panfrost/vulkan/libvulkan_panfrost.so

# 发布 testN（先 git commit，再构建，否则 Mesa SHA 不对）
cd /root/mali-g2-project
SO=driver/build-android-arm/src/panfrost/vulkan/libvulkan_panfrost.so
mkdir -p tools/artifacts/testN
llvm-strip -o /tmp/tN.so $SO
zstd -q -f $SO -o tools/artifacts/symbols/testN-unstripped.so.zst   # 符号版，用于还原崩溃地址
python3 driver/android/package.py /tmp/tN.so tools/artifacts/testN testN
rm -f /storage/emulated/0/Download/panvk-mali-g2-test*.zip            # Download 只留当前包
cp tools/artifacts/testN/*.zip /storage/emulated/0/Download/
git push -q origin main
```

包名含标签、提交和库哈希，保证 Eden 会替换驱动（同名包 Eden 不会重新加载）。
注意：修改 `pan_shader_info` 等结构后需重建 host 的 `panfrost_compile` 并删除 `libpan_shaders_v*`，否则预编译 shader 过期。

## 3. 本地 GPU 测试（无需 Eden）

统一前缀：`E="env -u LD_PRELOAD LD_LIBRARY_PATH=/vendor/lib64:/vendor/lib64/egl:/system/lib64"`，
`NEW=/root/mali-g2-project/driver/build-android-arm/src/panfrost/vulkan/libvulkan_panfrost.so`，在 `tools/` 下运行。

| 命令 | 用途 |
| --- | --- |
| `$E MALI_G2_TEST_FRAMES=5 build/bin/vulkan_draw $NEW build/shaders/texture.vert.spv build/shaders/texture.frag.spv texture` | 基础绘制+纹理；环境变量见 `vulkan_draw.c`（`MALI_G2_TEST_VERTS/INSTANCES/NO_DRAW/SWIZZLE/TEX_FORMAT/ALPHA_BLEND/LOGIC_OP/VERTEX_BINDINGS`） |
| `python3 test_eden_compute.py $NEW --tag tX` | Eden 计算着色器套件（应 9 PASS） |
| `build/crashprobe/oob $NEW build/crashprobe/oob.spv <stride> <robust>` | SSBO 越界钳制 |
| `build/crashprobe/preload $NEW <VkFormat> 1280 720 <iters> [clear_in_pass] [force_fault]` | 空 LOAD/STORE 渲染通道保留内容；`force_fault=1` 人为制造 GPU fault，用于验证 fault 报告 |
| `build/crashprobe/wg $NEW build/crashprobe/wg1024.spv 1 0` | 1024 线程工作组 + shared + barrier |
| `build/devcaps/devcaps <driver.so>` | 打印全部 feature/limit/扩展；原厂与 PanVK 对比结果在 `work/eden-test28/caps-*.txt` |
| `build/bin/replay_nir <shader-*.bin> out.o`（`MALI_G2_REPLAY_PRINT_NIR=1` 打印 NIR 到 stderr） | 重放 monitor 捕获的着色器 |

crashprobe 源码在 `tools/build/crashprobe/*.c`，编译：
`env -u LD_LIBRARY_PATH -u LD_PRELOAD /data/data/com.termux/files/usr/bin/clang -no-canonical-prefixes -std=c11 -O2 -I$R/references/malibridge/third_party/Vulkan-Headers-1.4.341/include -I$R/driver/include -I$R/driver/include/android_stub x.c -o x -ldl`（`R=/root/mali-g2-project`）。

## 4. 读取日志

### 4.1 驱动调试开关

驱动读取 Android 属性 `debug.mesa.panvk.debug`（等同 `PANVK_DEBUG`），Eden 无法传环境变量，只能用属性：

```sh
timeout 30 /system/bin/sh -c "su -c '/system/bin/setprop debug.mesa.panvk.debug sync; /system/bin/getprop debug.mesa.panvk.debug'"
```

- `sync`：每次 submit 后等待完成；开启 G2 fault 诊断（crumbs、work 日志、描述符回读）。当前保持 `sync`。
- `noafbc`：全局禁用 AFBC（可与 sync 组合：`sync,noafbc`）。
- `trace`：会让 Eden 崩溃，不可用。
- 备用：`/storage/emulated/0/Download/panvk-debug.txt` 文件内容也会被读取。

### 4.2 驱动事件日志（driver monitor）

驱动通过 UDP 127.0.0.1:28643 发送 `PANVK_G2_V1 ...` 事件和着色器 NIR blob。测试前在后台启动：

```sh
cd /root/mali-g2-project/tools
mkdir -p work/eden-testN/nir
python3 driver_monitor.py --output /storage/emulated/0/Download/panvk-g2-testN-driver.log \
  --capture-dir /root/mali-g2-project/tools/work/eden-testN/nir --seconds 28800 --sockets 16
```

每行是 JSON：`{"received":..., "event":"PANVK_G2_V1 pid= tid= t= <消息>"}`。测试结束后把 log 移到 `work/eden-testN/driver.log`。
驱动也会追加写 `Download/panvk-g2-driver-events.log`（≤8MB，仅当该文件已存在）。

关键事件：

| 事件 | 含义 |
| --- | --- |
| `device begin build=... threads_wg=...` | 确认 Eden 加载的是哪个构建（注意 SHA 可能因未重新生成而滞后，用 threads_wg 等字段辅助判断） |
| `fault crumbs done vt/frag/cs ... started ...` | 每个子队列最后完成/开始的工作：`serial<<32 | work 序号`；started>done 的那个就是卡住/出错的工作 |
| `fault submit=.. cmdbuf=.. serial=.. work=N` | 出错 submit 中的命令缓冲及其工作数 |
| `fault work=K draw/dispatch/copy/frag ...` | 每项工作：shader id、push 常量、拷贝地址、FBD/附件信息（`info` 位：level[0:8] layer[8:20] format[20:30] load[32:36] live[36] afbc[37]） |
| `fault res/baddesc/words` | 回读的资源表；`baddesc` = 指向非 live BO 的描述符 |
| `freed va=...` | 最近释放的 BO（判断 use-after-free） |
| `badrange kind=...` | 录制时检查到描述符/缓冲越界（带 backtrace） |
| `crash signal=... frame=N lib=libvulkan_panfrost.so off=0x...` | 进程信号；用符号版还原：`zstd -d -c tools/artifacts/symbols/testN-unstripped.so.zst > /tmp/x.so; llvm-symbolizer -e /tmp/x.so 0x<off>` |
| `desc/vsattr/vsbuf shader=..` | sync 模式下的描述符/顶点属性转储 |

着色器捕获文件：`work/eden-testN/nir/shader-<pid>-<id>-<stage>-<point>.bin`，用 `replay_nir` 打印 NIR。

### 4.3 Eden 日志（需 root）

```sh
B=/storage/emulated/0/Android/data/com.miHoYo.Yuanshen.nightly/files/log
cd /root/mali-g2-project/tools/work/eden-testN; mkdir -p eden_logs
timeout 60 /system/bin/sh -c "su -c 'cp $B/eden_gpu.log $B/eden_gpu.log.old.txt $B/eden_log.txt $B/eden_log.txt.old.txt /storage/emulated/0/Download/; dmesg > /storage/emulated/0/Download/dmesgN.txt'"
for f in eden_gpu.log eden_gpu.log.old.txt eden_log.txt eden_log.txt.old.txt dmesgN.txt; do
  [ -f /storage/emulated/0/Download/$f ] && mv /storage/emulated/0/Download/$f eden_logs/; done
```

- `eden_log.txt`：当前会话；`.old.txt` 是上一次会话（Eden 每次启动轮转，游戏会话常在 `.old.txt`）。已开启 Vulkan validation，`DebugUtilCallback` 行是验证层报错；`ReportLoss: Device loss occurred!` 是 DEVICE_LOST。
- `eden_gpu.log`：用户开启的详细 GPU 日志，含 `[Vulkan] vkCmdDraw*/vkCmdDispatch`、`[RenderPass] Begin/End renderArea=`、`[Pipeline] Bind`、`[Memory] Allocated`，可与驱动 work 序号按顺序对应。
- `gpu_crashes/` 一般为空。

### 4.4 内核日志（dmesg）

`su -c 'dmesg'`。关键行：
- `Unhandled Page fault ... at VA 0x...` + `TRANSLATION_FAULT ... READ ... Shader core N, lsc`：着色器/预载读到未映射地址；其后列出所有 GPU 映射（SAME_VA zone，`Imported UMM` 为 Eden 导入的 1280x720 交换链图像，每个 0x1d8a000）。
- `CS_FATAL ... CS_BUS_FAULT` / `CS_FAULT ... INSTR_INVALID_ENC`：通常是上面 page fault 的连带结果。
- `couldn't handle the OoM event`：tiler heap 用尽（test25 已修，`PANVK_G2_TILER_MAX_CHUNKS` 默认 1024）。

## 5. 当前问题与结论

用户报告的症状：Eden 进游戏后闪退（GPU page fault → DEVICE_LOST）；会动的动画窗口和滚动文字出现黑块（静态文字正常，黑块一直存在，与 AFBC 无关）；3D 不显示（原厂驱动在 Eden 里也不显示 3D，但在 Winlator/盖世游戏 可以）。

| 版本 | 改动 | 结果 |
| --- | --- | --- |
| test25 | tiler heap 上限 1024 chunk | 仍闪退 |
| test26 | 子队列 start/done crumbs，fault 时回读资源表 | 定位到 fragment 子队列卡住 |
| test27 | 记录每个 render pass 的 FBD 和附件信息 | fault 在一个 1280x720 render pass，读取 AFBC D24S8 深度平面的 RGB8 视图（meta copy）时越界数百 MB |
| test27 + `noafbc` | 属性禁用 AFBC | **能进游戏不闪退**；黑块仍在；3D 不显示 |
| test28 | v15 上 ZS 图像不用 AFBC；`maxComputeWorkGroupSize` 由内核报的 256 提升到 1024（本机实测 1024 正确，原厂也暴露 1024） | **闪退，比 test27+noafbc 差** |

test28 闪退分析（`work/eden-test28/`）：dmesg 有 `Unhandled Page fault at VA 0x7355E16D00`，随后 CSI1 `INSTR_INVALID_ENC`；Eden `ReportLoss`。fault 时的 work 列表里有大量 `dispatch shader=2855`，其 push 常量 `pc1=0x1000000010 / 0x2000000010 / 0x8000000020` 很像工作组尺寸，说明 1024 线程工作组改动后 Eden 换用了新的计算路径。可能原因（尚未验证）：
1. 1024 线程工作组在大 shader（寄存器多/有 spill/TLS）下不成立：内核报 256 可能是因为寄存器文件只够 256 线程跑满 64 寄存器的 shader。wg1024 测试 shader 很小，没有覆盖这种情况。
2. 非 ZS 图像仍是 AFBC，还有其他 AFBC 路径出问题（noafbc 时没闪退）。

建议下一步：
1. 先单独回退 1024 工作组（或按 shader 寄存器数限制，`work_reg_count > 32` 时只允许 ≤ 512/256），保留 ZS 无 AFBC，验证闪退是否消失；再用 `sync,noafbc` 对照。
2. 黑块：只出现在每帧变化的内容上 → 检查每帧上传/拷贝的纹理（buffer→image 拷贝、staging、image 视图格式重解释、`VK_FORMAT_A1B5G5R5` 等）；可在 `vulkan_draw`/crashprobe 用 >16x16 的非纯色图案测 copy→sample→render→copy 往返（`tools/build/crashprobe/tiles/` 已写好 shader：`sample.comp`、`quad.vert`、`coord.frag`，宿主程序还没写）。
3. 3D：原厂驱动在 Eden 同样不显示，先别当成 PanVK 独有 bug。与原厂相比 PanVK 缺：geometryShader、tessellationShader、multiViewport/shaderOutputViewportIndex、transformFeedback、16x MSAA、fragment_shader_barycentric、external_memory_host（见 `work/eden-test28/caps-*.txt`）。clip/cull distance、fillModeNonSolid 原厂也没有。fork 里已有 GS 仿真 `driver/src/panfrost/vulkan/panvk_nir_lower_gs.c`，但只在 v6/v7 启用；移植到 v15 是补齐 1.4 特性的主要工作。用户希望最终与原厂特性对齐。

已排除（不必重复）：描述符/动态/顶点/索引/间接缓冲地址越界、bound set、copy 地址、freed BO 复用、双队列、CPU cache flush、tiler reclaim、copy/present shader、SSBO 越界钳制（硬件正确）、顶点越界 fetch、空 LOAD/STORE 通道（RGBA8/BGRA8/RGB10A2/D24S8/D32S8/D32 均正确）。

## 6. 关键代码位置

| 位置 | 内容 |
| --- | --- |
| `driver/src/panfrost/vulkan/panvk_g2_trace.c/.h` | 事件发送、崩溃处理、`panvk_g2_check_range`、fault 时资源表/内存回读 |
| `driver/src/panfrost/vulkan/csf/panvk_cmd_buffer.h` | `g2_draws` 工作日志（每项 8×u64）、`panvk_g2_crumb/_start` |
| `driver/src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c` | sync 模式 fault 报告（搜 `fault crumbs`） |
| `driver/src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c` / `_dispatch.c` / `panvk_vX_cmd_meta.c` | 工作记录点 |
| `driver/src/panfrost/lib/kmod/kbase_kmod.c` | kbase 后端；live BO 表、freed ring、GPU 属性（含 1024 工作组覆盖） |
| `driver/src/panfrost/vulkan/panvk_image.c` `panvk_image_can_use_mod` | AFBC 选择（含 v15 ZS 禁用） |
| `driver/src/panfrost/vulkan/panvk_physical_device.c` | v15 tiler heap 参数、队列数 |
| `driver/src/panfrost/compiler/bifrost/valhall/va_pack.c` | v15 编码与 preload 映射 |
| `references/eden-upstream/src/src/video_core` | Eden 渲染器源码（查调用方式） |

## 7. 测试流程（与用户协作）

1. 构建、本地测试、提交、打包 testN 放入 Download（只留一个 ZIP），推送 GitHub。
2. 确认 `debug.mesa.panvk.debug` 为所需值，后台启动 driver monitor。
3. 用户在 Eden 安装 ZIP 并进游戏，反馈现象（中文）。
4. 按第 4 节收集 driver log、Eden 日志、dmesg 到 `tools/work/eden-testN/`，分析。
用户偏好：不要反复提问，自主决定并直接做；报告要说清楚是“修复”还是“诊断版”。
