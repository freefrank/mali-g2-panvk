# 构建与测试

## 环境

- 项目根目录：`/root/mali-g2-project`
- Meson：`/root/mali-g2-project/toolchain/bin/meson`
- Android Clang：`/data/data/com.termux/files/usr/bin/clang`
- Android 原生辅助程序必须使用 `-no-canonical-prefixes`
- Mesa host build：`/root/mali-g2-project/driver/build-host`
- Mesa Android build：`/root/mali-g2-project/driver/build-android-arm`
- Vulkan headers：`/root/mali-g2-project/references/malibridge/third_party/Vulkan-Headers-1.4.341/include`

现有 `driver/build-host` 和 `driver/build-android-arm` 已重新配置，可使用下方命令增量构建。

所有可重建输出统一写入 `build/`，不再写到仓库根目录。

重建要点：

- host 配置需要 `PKG_CONFIG_PATH=/root/mali-g2-project/toolchain/deps/spirv-tools/lib/pkgconfig`，系统 SPIRV-Tools 2023.6 太旧。
- Android 交叉文件位于 `toolchain/android-cross.ini`，放在构建目录外，`meson setup --wipe` 不会删除它。
- Termux clang 在 `-static-libstdc++` 时链接 `-lc++_shared`；`toolchain/android-libcxx/libc++_shared.{a,so}` 是指向 NDK `libc++_static.a` + `libc++abi.a` 的链接脚本，`cpp_link_args` 需要包含 `-L/root/mali-g2-project/toolchain/android-libcxx`。
- `cpp_link_args` 还必须直接列出 NDK 的 `libc++_static.a` 和 `libc++abi.a`。否则 C++ 运行时符号（`_Znwm`、`__gxx_personality_v0` 等）会被 gralloc 桩 `libs-a12/libc++.so` 解析，驱动多出 `NEEDED libc++.so`，而这个库不在应用可加载的公共库里，Eden 创建设备时就会闪退（test8 出过这个问题）。构建完成后用 `llvm-readelf -d` 确认 NEEDED 列表里没有 `libc++.so`。
- 修改 `src/panfrost/compiler` 中的结构体（如 `pan_shader_info`）后，必须先重编 `build-host` 的 `panfrost_compile` 并复制到 `build-host/bin`，再删除 `build-android-arm/src/panfrost/libpan/libpan_shaders_v*.[ch]` 重新生成，否则预编译着色器的元数据布局与驱动不一致。
- 构建 Android 驱动前需重新生成 gralloc 桩：`android/gralloc/mkstubs.py`（见 `driver/build.sh`）。

## 构建辅助程序

```sh
cd /root/mali-g2-project/tools
env LC_ALL=C python3 build_helpers.py --android
env LC_ALL=C python3 build_helpers.py --android-replay
```

输出：

- `build/bin/compile_nir`
- `build/bin/replay_nir`
- `build/bin/replay_nir_android`
- `build/bin/kmd_probe`
- `build/bin/probe_binaries`
- `build/bin/vulkan_android`
- `build/bin/vulkan_draw`
- `build/bin/vulkan_compile`
- `build/bin/libtrace_ioctl.so`

## 构建 Android 驱动

```sh
env LC_ALL=C \
  PATH=/root/mali-g2-project/driver/build-host/bin:/root/mali-g2-project/toolchain/bin:/usr/bin:/bin:/usr/sbin:/sbin \
  ninja -C /root/mali-g2-project/driver/build-android-arm -j3 \
  src/panfrost/vulkan/libvulkan_panfrost.so
```

驱动输出：

```text
/root/mali-g2-project/driver/build-android-arm/src/panfrost/vulkan/libvulkan_panfrost.so
```

## 基础 GPU 测试

```sh
cd /root/mali-g2-project/tools
python3 test_android.py /path/to/libvulkan_panfrost.so --tag local-test
python3 test_backend.py --output fixtures/local-backend.json
python3 test_eden_compute.py /path/to/libvulkan_panfrost.so --tag local-eden
```

测试生成的 SPIR-V 位于 `build/shaders/`，结果位于 `fixtures/`。

## 性能版与诊断开关

从 test37 起，`PANVK_G2_DIAGNOSTICS` 默认关闭。此时会忽略 PANVK_DEBUG（包括 Android 残留 sync 属性），不启动日志、转储、地址检查、崩溃处理器或 GPU breadcrumb。正常游戏使用默认状态；用户已明确要求仅在出问题时再开启。

原生诊断子进程使用 `PANVK_G2_DIAGNOSTICS=1 PANVK_DEBUG=sync`。Android 排错时，对应属性为 `debug.mesa.panvk.g2.diagnostics=1`；关闭设为 0。修改后重启模拟器。不要在常规回归或正常游戏期间遗留开启状态。

```sh
python3 test_diagnostics.py /path/to/new/libvulkan_panfrost.so \
  --baseline work/eden-test36/libvulkan_panfrost.so --tag test37-diagnostics
```

该审计在本机 UDP 28643 端口临时接收数据，执行后自动关闭。不要同时运行 driver_monitor。测试覆盖默认关闭、显式关闭、显式开启和旧版对照，验证 socket、信号 handler、日志与实际绘制。`test_log_capture.py` 会只在自身子进程里显式开启诊断，不改变 Android 全局属性。

## MSAA 解析回归

`build_helpers.py --android` 构建 `vulkan_msaa`。该工具在有效的 sampled/color attachment/transfer 用途下，清除多采样图像，逐样本读取验证，再执行 CmdResolveImage 并验证最终颜色；数组用例同时解析三层、只给最后一层非零颜色。覆盖 2/4/8/16 倍采样与 RGBA8/RGBA16F/R11G11B10：

```sh
python3 test_msaa.py /path/to/libvulkan_panfrost.so --tag test36-msaa \
  --baseline work/eden-test35/libvulkan_panfrost.so
```

脚本会构建采样 shader。`--baseline` 可省略；指定时预期旧库只在目标解析上失败，而非设备丢失。报告包含原厂、新驱动和旧库各自结果。数组两次重叠清除之间必须保留 WAW 屏障，否则测试本身有竞争。

## Eden 日志读取集成验证

先运行 `driver_monitor.py`，再用 `test_log_capture.py` 检查驱动是否能从进程打开的日志中读取 renderer、视频和 Critical 记录。此测试通过原生 GPU 程序继承 write-only 日志 FD，日志名分别为 `eden_log.txt` 和 `yuzu_log.txt`，并同时验证带深度、索引和背景 LOAD 的真实绘制：

```sh
python3 test_log_capture.py /path/to/libvulkan_panfrost.so \
  --monitor /storage/emulated/0/Download/panvk-g2-test34-driver.log --tag test34-log-fd
```

日志路径通过 `/proc/self/fd` 自动发现，避免应用更改包名或用户目录后继续使用无效的固定路径。测试报告位于 `fixtures/<tag>.json`。

Eden renderer_debug 会启用 adrenotools 对驱动 fopen 的重定向。必须同时测试这一环境；仅运行上面的无 hook 用例不足以验证应用内采集：

```sh
env -u LD_LIBRARY_PATH -u LD_PRELOAD /data/data/com.termux/files/usr/bin/clang \
  -no-canonical-prefixes -std=c11 -Wall -Wextra -Werror -O2 -shared -fPIC \
  log_redirect_hook.c -o build/bin/liblog_redirect_hook.so -ldl
python3 test_log_capture.py /path/to/libvulkan_panfrost.so \
  --monitor /storage/emulated/0/Download/panvk-g2-test35-driver.log \
  --tag test35-log-hook --fopen-hook build/bin/liblog_redirect_hook.so
```

模拟 hook 只拒绝读取两个应用日志名称，允许测试程序正常读取 SPIR-V。test34 归档库应出现采集失败、绘制仍成功；test35 应全部通过。驱动只为此诊断读取使用直接 openat 系统调用，其余文件访问保持原有实现。


## 深度与索引绘制回归

`vulkan_tiles` 可用 `MALI_G2_TILES_DEPTH_FORMAT=124/126/129/130` 添加 D16/D32/D24S8/D32S8 附件。配合 `fixtures/tiles_depth.vert`（z=0.5），`MALI_G2_TILES_REVERSE_DEPTH=1` 使用 GREATER，`MALI_G2_TILES_REJECT_DEPTH=1` 改变清除深度，使片元必须被遮挡。CPU 验证同时检查实际颜色与应保留的背景。

```sh
glslangValidator -V fixtures/tiles_depth.vert -o build/shaders/tiles_depth.vert.spv
env -u LD_PRELOAD LD_LIBRARY_PATH=/vendor/lib64:/vendor/lib64/egl:/system/lib64 \
  MALI_G2_TILES_DEPTH_FORMAT=129 MALI_G2_TILES_WHITE=1 MALI_G2_TILES_BLEND=1 \
  MALI_G2_TILES_SCISSOR=1 MALI_G2_TILES_LOAD_DRAW=1 MALI_G2_TILES_PARTIAL_RENDER=1 \
  build/bin/vulkan_tiles /path/to/libvulkan_panfrost.so \
  build/shaders/tiles_sample.comp.spv 257 129 4 37 \
  build/shaders/tiles_depth.vert.spv build/shaders/tiles.frag.spv
```

`MALI_G2_TILES_INDEX_BITS=16/32` 改用索引绘制，firstIndex 固定 7；`MALI_G2_TILES_VERTEX_OFFSET=-1` 或 `-30000` 可验证非零索引和负偏移组合。测试报告为 `fixtures/test33-depth.json` 和 `fixtures/test33-indexed.json`。这些 shader 不读取顶点属性，索引用例不覆盖顶点缓冲属性读取。

## 大尺寸动态纹理回归

`build_helpers.py --android` 会构建 `build/bin/vulkan_tiles`。在 `tools/` 下运行：

```sh
glslangValidator -V fixtures/tiles_sample.comp -o build/shaders/tiles_sample.comp.spv
env -u LD_PRELOAD LD_LIBRARY_PATH=/vendor/lib64:/vendor/lib64/egl:/system/lib64 \
  build/bin/vulkan_tiles /path/to/libvulkan_panfrost.so \
  build/shaders/tiles_sample.comp.spv 257 129 8 37
```

参数依次为驱动、采样 shader、宽、高、帧数、VkFormat。格式 37 为 RGBA8，44 为 BGRA8，6 为 R5G5B5A1；另支持 R8、RG8、RGB565、ARGB1555，以及 131/137/145/157（BC1/BC3/BC7/ASTC 4×4）。程序每帧更新整幅图案，验证上传后原始数据回读与计算采样，成功输出 `TILES_PASS`，失败返回非零。

test30 的六个场景、原厂对照和 test29 故障结果保存在 `fixtures/test30-tiles.json`。RGBA8 大图还应覆盖 1280×720 和 2560×1440。

增加实际图形绘制验证时，先编译 `fixtures/tiles.vert` 和 `fixtures/tiles.frag`，再将两个 SPIR-V 路径追加到命令最后。图形输出也会逐像素比较。`MALI_G2_TILES_BLEND=1` 启用透明混合（允许 1 LSB 的舍入差），`MALI_G2_TILES_SCISSOR=1` 每帧移动裁剪框，`MALI_G2_TILES_LOAD=1` 在绘制后加空 LOAD/STORE 通道验证保留内容，`MALI_G2_TILES_ALPHA_R=1` 使用 `(1,1,1,R)` 遮罩 swizzle。`MALI_G2_TILES_MIP=2`、`MALI_G2_TILES_LAYER=2` 选取非零 mip/数组层，传入宽高仍是选定 mip 的大小。压缩纹理使用合法、每帧变化的常色块，原始块回读按字节比较。test31 的基线失败、新构建通过和原厂对照见 `fixtures/test31-textures.json`。

## LOAD 后透明绘制回归

在大尺寸纹理测试命令后追加 `tiles.vert.spv`、`tiles.frag.spv`，并设置 `MALI_G2_TILES_WHITE=1 MALI_G2_TILES_BLEND=1 MALI_G2_TILES_SCISSOR=1 MALI_G2_TILES_LOAD_DRAW=1`。程序先清白底，再切换到 LOAD 通道实际绘制，逐像素验证背景保留。`MALI_G2_TILES_PARTIAL_RENDER=1` 同时验证非零起点的局部 renderArea；`MALI_G2_TILES_RT_FORMAT` 选择目标附件格式（37/44/58/91/97/109/122）。空 LOAD 通道可能被省略，不能替代这个测试。

`MALI_G2_TILES_SPLIT_UPLOAD=1` 将上传分为四个区域，`MALI_G2_TILES_IMAGE_COPY=1` 加入 image→image 拷贝，`MALI_G2_TILES_REBIND=1` 在 LOAD 通道中重绑状态以排查状态恢复问题。`MALI_G2_TILES_DUMP_RENDER=<path>` 按帧保存目标原始字节，可与原厂严格比较。R11G11B10_FLOAT 的 CPU 模型有舍入局限，见 `fixtures/test32-framebuffer-load.json` 中的原厂逐字节对照。

## 32 顶点输入复现

先编译两个 shader：

```sh
mkdir -p build/shaders
glslangValidator -V fixtures/vertex_input32.vert -o build/shaders/vertex_input32.vert.spv
glslangValidator -V fixtures/vertex_input32.frag -o build/shaders/vertex_input32.frag.spv
```

原厂驱动测试：

```sh
env -u LD_PRELOAD \
  LD_LIBRARY_PATH=/vendor/lib64:/vendor/lib64/egl:/system/lib64 \
  MALI_G2_TEST_FRAMES=30 \
  MALI_G2_TEST_VERTEX_BINDINGS=32 \
  build/bin/vulkan_draw \
  /vendor/lib64/hw/vulkan.mali.so \
  build/shaders/vertex_input32.vert.spv \
  build/shaders/vertex_input32.frag.spv vertex_input
```

`MALI_G2_TEST_IGNORE_VERTEX_LIMITS=1` 只用于独立测试程序复现旧驱动越界，不能用于正常应用或能力验证。

## 批量重放捕获

```sh
env -u LD_PRELOAD \
  LD_LIBRARY_PATH=/vendor/lib64:/vendor/lib64/egl:/system/lib64 \
  MALI_G2_REPLAY_THREADS=2 \
  build/bin/replay_nir_android --batch work/research/fixtures/eden-test7-replay-list.txt
```

未使用 Android root 时，直接读取 Eden 日志受 scoped storage 限制；公共 Download 的旧导出也不能替代本轮日志。已有 root 时使用下方原生 Termux 采集方法，无需用户重新导出。

Policy marker: biblioklept

## 打包

`python3 driver/android/package.py <stripped.so> <输出目录> <标签>`。标签、提交和库哈希会写进 meta.json 的 name/packageVersion 和 ZIP 文件名；名称相同的包 Eden 不会替换已安装的驱动。

## Root 与崩溃记录

Termux 已有 Android root。旧会话能用 `/system/bin/sh -c 'su -c "logcat -d -b crash"'`，当前 PRoot 中这条命令 ENOENT；已通过原生 Termux 服务验证 `uid=0(root) context=u:r:ksu:s0`。不能把 PRoot 里的假 UID 0 当作 Android root，也不能把该 ENOENT 当作设备没 root。

这台设备安装的是 Play 商店版 Termux，没有 `RunCommandService`。它提供 `com.termux.app.TermuxService`，action 为 `com.termux.service.action.service_execute`。写一个只采集所需日志的一次性 Android shell 脚本，脚本中设 `PATH=/system/bin:/system/xbin`、`TERMUX_EXEC_OPTOUT=1`，清除 `LD_PRELOAD`、`LD_LIBRARY_PATH`，再用 `/system/bin/su -c '只读采集命令'`。脚本输出到公共 Download 后复制进 `tools/work/`。

必须由 `/system/bin/sh` 读取脚本；直接把 `.sh` 作为服务的 executable 会被当作 ELF 并报 bad ELF magic。调用示例中的脚本路径是 Android 原生路径，不能传 PRoot 的 `/root/...`：

```sh
env -u LD_PRELOAD -u LD_LIBRARY_PATH \
  /data/data/com.termux/files/usr/bin/bash \
  /data/data/com.termux/files/usr/bin/am startservice --user 0 \
  -n com.termux/com.termux.app.TermuxService \
  -a com.termux.service.action.service_execute \
  -d file:///system/bin/sh \
  --esa com.termux.execute.arguments \
  /data/data/com.termux/files/usr/var/lib/proot-distro/containers/base/rootfs/root/mali-g2-project/tools/work/eden-test38/native-log.sh \
  --ez com.termux.execute.background true
```

成功采集脚本和 root 输出位于 `tools/work/eden-test38/`。没有修改外部应用权限设置，没有常驻 root shell 或日志监视器。服务接口依据：[Play 商店版 TermuxService 源码](https://github.com/termux-play-store/termux-apps/blob/main/termux-app/src/main/java/com/termux/app/TermuxService.java)。

## 产物布局

- `tools/artifacts/<版本>/` 只保存交付 ZIP（内含剥离后的驱动）。
- `tools/artifacts/symbols/<版本>-unstripped.so.zst` 保存带符号驱动，用于还原 tombstone 地址：`zstd -d -c <文件> > /tmp/x.so`，再用 `llvm-symbolizer -e /tmp/x.so <偏移+0x4000>`（tombstone 的 pc 加上 `(offset …)` 再加可执行段的 vaddr−offset 差）。
- 历次 Eden 驱动事件日志压缩在 `tools/work/driver-logs/`。
- Download 只放当前在测的 ZIP。

## G2 调试开关

- `su -c "/system/bin/setprop debug.mesa.panvk.debug sync"`：每次提交等待 GPU 完成，便于对齐出错提交。
- `trace` 模式在 Eden 下会让驱动在纹理描述符写入处崩溃，暂不可用。
- v15 驱动在描述符写入时检查缓冲区/纹理地址是否落在当前分配的 BO 内，越界会在事件日志里记 `badrange` 和调用栈。


## test38 颜色 AFBC 验证

`vulkan_tiles` 可设 `MALI_G2_TILES_COMPRESSION=probe`，开启受支持的 image compression control 扩展并通过 `vkGetImageSubresourceLayout2EXT` 链接 `VkImageCompressionPropertiesEXT` 查询实际压缩属性；`off` 则使用 Vulkan 的显式禁用压缩控制。测试不打开驱动诊断。PanVK 的 DEFAULT=0 对应 AFBC，DISABLED=0x4 对应未压缩，其他厂商的 DEFAULT 不应直接解释为 AFBC。参考：[Vulkan 查询接口](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetImageSubresourceLayout2.html)。

`tools/fixtures/test38-afbc-upload.json` 记录六个原上传用例；`test38-afbc-extended.json` 是 18 个图形/图层/格式用例；`test38-msaa.json` 是 24 个当前驱动与 24 个原厂对照。R11 float 混合的旧 CPU oracle 容差不足，原厂和 test37 也失败；`test38-r11-blend-reference.json` 比较四帧完整 raw bytes，三者 SHA 相同，未放宽 oracle。不要将初始 oracle 失败声称为 AFBC 回归，也不要把输出一致声称为覆盖全部 R11 混合精度规范。

应用侧性能采样和配置回读在 `tools/work/eden-perf38/`。所有 simpleperf 已结束，没有常驻采样或驱动日志接收器。当前设备原生 GPU 利用率只读节点是 `/sys/class/devfreq/gpufreq_core/dynamic/util` 和 `polling_history`；`gpufreq_usage` 是驻留表。

## test39 深度/模板 AFBC 验证

`vulkan_tiles` 新增 `MALI_G2_TILES_DEPTH_READBACK=1`，验证实际深度值和模板 0x5a；`MALI_G2_TILES_DEPTH_ROUNDTRIP=1` 还会将回读值上传回图像再验证。上传前先清为 depth=0.125、stencil=0xc3，避免空操作也通过。配合 `MALI_G2_TILES_DEPTH_FORMAT=124/126/129/130` 选择 D16/D32/D24S8/D32S8，顶点着色器使用 `tiles_depth.vert.spv`。

`test39-depth-roundtrip.json` 覆盖原厂、test38、候选共 12 项；`test39-depth-extended.json` 覆盖 LOAD、partial render、discard、反向和拒绝深度及 720p/1440p 共 26 项。测试脚本在 `tools/work/eden-test39/run_depth_matrix.py`，候选实际压缩属性通过前述 compression probe 验证。其他回归报告、清单、源码和增量补丁见 `tools/fixtures/test39-*.json`、`tools/artifacts/test39/`。

原生 root 无法直接打开应用私有驱动路径时，可读取当前进程 maps 中的驱动映射范围，再从 `/proc/PID/map_files/START-END` 复制完整 ELF；test38 的实际加载库已通过该方法与包内 SHA-256 对齐。采集脚本 `tools/work/eden-perf39/collect.sh`，有效前台采样 `active-profile.sh`，结果分别保存在 `inactive-capture/` 和 `active-capture/`。首次暂停采样不代表用户报告的低帧场景；摘要见 `profile-summary.json`。

## test41 压缩纹理 U 平铺验证

内部 raw 复制使用线性别名：每个物理 4×4 压缩块 tile 展开为 16 个普通格式 texel，列坐标按 U 曲线映射（X xor Y），行坐标为块 Y 除以 4。原生压缩采样仍使用真实 U 布局。应用要求 block-texel 视图时保留线性布局，避免将内部坐标变换施加给应用 shader。

`vulkan_tiles` 的 BC 色块随位置和帧变化，先用原厂与 test39 验证 oracle。`MALI_G2_TILES_DUMP_RAW=路径` 保存每帧源与回读原始块；`MALI_G2_TILES_DUMP_IMAGE=路径` 保存同步后的完整图像内存，输出 allocation 大小；`test41-layout-proof.json` 将这些字节与独立 CPU U 曲线、线性布局逐块比较，排除意外线性回退。映射内存检查只对本驱动实现解释物理布局。

`MALI_G2_TILES_CREATE_FLAGS=0x88` 在候选中显式要求 mutable + block-texel 视图，强制线性对照。`MALI_G2_TILES_COPY_FLAGS` 可独立控制复制目标，覆盖线性↔平铺复制。`MALI_G2_TILES_COPY_VIA_RGBA16=1` 与 IMAGE_COPY 一起让 BC1 经 RGBA16 中间图像往返复制，用 `TILES_COPY_VIA_RGBA16` 标记确认走了新路径；compression probe 确认 PanVK 中间图像实际为 AFBC。当前该用例限定 mip0，支持数组层。

`MALI_G2_TILES_TIMING=1` 使用 GPU timestamp 分别测计算采样和片元绘制。`test41-texture-timing.json` 是同一候选库的线性/平铺 ABBA 对照，受系统并发与频率影响，结果不统一，不能作为游戏帧率承诺。32 项纹理矩阵脚本为 `tools/work/eden-test41/run_texture_matrix.py`；源补丁、相对 test39 增量、所有报告哈希与符号位置见 `tools/artifacts/test41/build.json`。

## test42 Android native fence 导入验证

`build_helpers.py --android` 构建 `vulkan_acquire`。单独编译延迟 shader 后，可以对不同驱动运行六种句柄组合：

```bash
glslangValidator -V tools/fixtures/acquire_delay.comp -o tools/build/shaders/acquire_delay.comp.spv
env -u LD_PRELOAD LD_LIBRARY_PATH=/vendor/lib64:/vendor/lib64/egl:/system/lib64 PANVK_G2_DIAGNOSTICS=0 tools/build/bin/vulkan_acquire DRIVER.so tools/build/shaders/acquire_delay.comp.spv both
```

末尾模式支持 `semaphore`、`fence`、`both`、`none`、`signaled`、`minusone`。测试创建两个 logical device，各使用 G2 实际支持的一个队列：生产者执行延迟计算，导出真实 Linux SYNC_FD；另一个设备调用 AcquireImageANDROID，再提交消费者。验证导入 fence 和导出回读的 semaphore 在 native fence 未触发时仍未就绪，随后检查完成状态、生产者计算结果及消费者输出。semaphore 导出后重新导入再供消费者等待。测试使用普通 VkImage，仅覆盖本驱动的同步导入路径，不代替真实窗口交换链测试。

`tools/work/eden-test42/run_acquire_matrix.py` 对 test41 与候选各运行六例；`tools/fixtures/test42-acquire.json` 保存结果。人为 GPU 延迟约 386 毫秒时，test41 的 semaphore/fence/both 获取调用阻塞约 386 毫秒，候选约 0.02–0.06 毫秒返回且完成信号保持未就绪。此数字只说明调用行为，不能解释为游戏每帧停顿或加速比例。无同步对象时保留原阻塞回退，已触发和 -1 fence 仍通过。

本轮其余报告为 `test42-android.json`、`test42-eden.json`、`test42-diagnostics.json`、`test42-textures.json`；从 ZIP 提取的 stripped 库另测三种延迟 native fence 和四种带 mip/layer/copy/LOAD 的压缩纹理，见 `test42-packaged-smoke.json`。源码、相对 test41 增量、报告哈希和符号位置见 `tools/artifacts/test42/build.json`。

## test43 完成线程与批量提交

`build_helpers.py --android` 构建 `vulkan_completion`。生产 GPU 工作在同次提交中 signal 普通 fence 与独立的可导出 semaphore；测试导出 semaphore 的 SYNC_FD 并等待它触发，再测普通 fence 等待返回。逐段核对随帧更新的复制结果，并读取 GPU timestamp。`notify` 是 CPU 观察到 native 完成之后的剩余等待下界，包含 CPU 调度，不能视为纯驱动延迟或游戏帧时间。

```bash
env -u LD_PRELOAD LD_LIBRARY_PATH=/vendor/lib64:/vendor/lib64/egl:/system/lib64 PANVK_G2_DIAGNOSTICS=0 MALI_G2_COMPLETION_IDLE_US=16000 tools/build/bin/vulkan_completion DRIVER.so 1 128 64
env -u LD_PRELOAD LD_LIBRARY_PATH=/vendor/lib64:/vendor/lib64/egl:/system/lib64 PANVK_G2_DIAGNOSTICS=0 MALI_G2_COMPLETION_BURST=1 tools/build/bin/vulkan_completion DRIVER.so 128 32 4096
```

三个数值为命令缓冲数、采样轮数、每段复制字节数，另有八轮预热。默认每轮一次提交多个命令缓冲；BURST 模式为每个命令缓冲单独 QueueSubmit 并分配普通 fence，最后一次提交再 signal 可导出 semaphore。等待所有 fence，检查全部复制段及预热后/设备 idle 后的 FD 数。压力组合覆盖 1/8/64/128 个独立提交和超过单轮 64 个完成回调的情况，FD 数为 9→9。

`test43-completion.json` 是 test42/候选在 0/2000/16000 微秒间隔下的 ABBA 对照，12 项均正确，但计时有波动，不能宣称所有尾延迟改善。`test43-completion-burst.json` 为四个压力组合。其余 native fence、Android、Eden、诊断、纹理、MSAA 与包内库报告及哈希索引在 `tools/artifacts/test43/build.json`。

游戏呈现间隔可以临时采集 SurfaceFlinger TimeStats，采集后禁用。**dump 会保留历史统计，非空不代表正在启用**；perf44 第一次窗口因此无效，已排除，不能复用旧脚本的空/nonempty 启用判断。应实际启用、记录窗口前后 dump，并按同一 SurfaceView/刷新率/renderRate 分组相减，解析脚本 `tools/work/eden-perf44/analyze_intervals.py`。不要同时运行本地 GPU 测试。匹配游戏 SurfaceView，不使用应用 VRI 图层或全局合成器帧率代替游戏；累计 averageFPS 也不是独立窗口平均。SurfaceView 无 Timeline 帧数时，jankyFrames=0 不能证明无卡顿。

## test44 重复缓存清理

`finish_cs` 在每个子队列等待全部 scoreboard、执行 LSC/L2 clean 并等待 flush 完成。kbase 提交环原先再次 clean。主命令流的内部 `cache_clean` 标记只在 G2、诊断关闭且没有 u_trace points 时生效，按最后一个 CALL 的契约决定是否省去外层重复 clean。原始初始化、未标记、空提交、诊断/trace 及其他架构仍走保守路径；入口 invalidate、命令流内部 clean、错误屏障和完成计数不变。

`vulkan_submit` 可读测试进程自己的非环绕 64KB Mali 映射，用重复 ERROR_BARRIER/MOVE/MOVE/SYNC_ADD64/WAIT 序列确认三条提交环，再数 CACHE_FLUSH2：

```bash
env -u LD_PRELOAD LD_LIBRARY_PATH=/vendor/lib64:/vendor/lib64/egl:/system/lib64 PANVK_G2_DIAGNOSTICS=0 MALI_G2_SUBMIT_SCAN_FLUSH=1 tools/build/bin/vulkan_submit DRIVER.so 1 32 64
```

这是本驱动实现的内部命令检查，不能用于原厂或任意驱动。短测试须避免 64KB 环回绕。test43/44 各环仍有 42 个结束标记，环内 flush 为 250→130，诊断开启为 250→250；命令缓冲自身的必要 clean 不计入这些环内数值。原来只跳过空队列的方案仅省三条，已被取代。LD_PRELOAD 方案没有加载，不作为证明。

`vulkan_completion` 增加 `MALI_G2_COMPLETION_EMPTY_GAPS=1`；与 BURST 一起，每个有复制工作的 QueueSubmit 后再插入带独立 fence 的空提交。核对所有工作/空提交的 fence、全部复制段和 FD 数。矩阵 `tools/work/eden-test44/run_cache_matrix.py`，报告索引 `tools/artifacts/test44/build.json`。CPU submit-to-fence 含外层清理，GPU timestamp 只包围命令体，不含末尾环清理；ABBA 有波动，不能当游戏性能承诺。

用户报告游戏让整机卡慢并确认结束本地测试后仍发生。此后不再运行合成 GPU 程序。交付验证改为比较 stripped/unstripped 全部 SHF_ALLOC 节的字节、地址、类型、大小和标志，再验 ZIP 与 Download 副本；27 个节均相同，见 `test44-packaged-integrity.json`。170 项运行/对照用例和三个文件检查分开记录。

Android root 的 `cmd`/`settings` 输出直接写共享存储可能触发 Failed transaction；用 `cmd ... </dev/null 2>&1 | cat > 输出文件` 可正常调用。只看退出码不够，须读回实际状态。屏幕支持值是 120.00001；即使 preferred mode 匹配成功，实际 activeMode 也可能仍为 60，不能把 setter 成功当作已锁 120。两次临时修改均已恢复。

本机 `csg_scheduling_period` 原值 100，单位为毫秒，接口说明见 [内核 ABI 文档](https://android.googlesource.com/kernel/google-modules/gpu/+/c009f0a10bf01666ee94f7a3f96e8ad1f122a2ed%5E%21/)。10 毫秒短对照无改善，已恢复 100，没有持久修改。debugfs 原未挂载；可在私有临时目录只读挂载，读完卸载并删除目录，不需要改全局 `/sys/kernel/debug`。本轮快照为游戏 Priority2、SurfaceFlinger Priority0，不代表完整抢占时序。脚本/原始数据在 `tools/work/eden-perf44/`，不要无依据修改优先级或时钟。
