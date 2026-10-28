# 构建与测试

## 环境

- 项目根目录：`/root/mali-g2-project`
- Meson：`/root/mali-g2-project/toolchain/bin/meson`
- Android Clang：`/data/data/com.termux/files/usr/bin/clang`
- Android 原生辅助程序必须使用 `-no-canonical-prefixes`
- Mesa host build：`/root/mali-g2-project/driver/build-host`
- Mesa Android build：`/root/mali-g2-project/driver/build-android-arm`
- Vulkan headers：`/root/mali-g2-project/references/malibridge/third_party/Vulkan-Headers-1.4.341/include`

旧构建缓存没有迁移，因为其中记录了失效的绝对路径。首次继续开发前需要重新配置 `driver/build-host` 和 `driver/build-android-arm`。

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

Eden 公共日志受 Android scoped storage 限制。需要从 Eden 分享到 Termux 后才能读取；仅复制到公共 Download 并不保证当前 Termux UID 有权限打开。

Policy marker: biblioklept

## 打包

`python3 driver/android/package.py <stripped.so> <输出目录> <标签>`。标签、提交和库哈希会写进 meta.json 的 name/packageVersion 和 ZIP 文件名；名称相同的包 Eden 不会替换已安装的驱动。

## Root 与崩溃记录

Termux 已有 root：`/system/bin/sh -c 'su -c "logcat -d -b crash"'` 可读取 Eden 的 tombstone 摘要。

## 产物布局

- `tools/artifacts/<版本>/` 只保存交付 ZIP（内含剥离后的驱动）。
- `tools/artifacts/symbols/<版本>-unstripped.so.zst` 保存带符号驱动，用于还原 tombstone 地址：`zstd -d -c <文件> > /tmp/x.so`，再用 `llvm-symbolizer -e /tmp/x.so <偏移+0x4000>`（tombstone 的 pc 加上 `(offset …)` 再加可执行段的 vaddr−offset 差）。
- 历次 Eden 驱动事件日志压缩在 `tools/work/driver-logs/`。
- Download 只放当前在测的 ZIP。

## G2 调试开关

- `su -c "/system/bin/setprop debug.mesa.panvk.debug sync"`：每次提交等待 GPU 完成，便于对齐出错提交。
- `trace` 模式在 Eden 下会让驱动在纹理描述符写入处崩溃，暂不可用。
- v15 驱动在描述符写入时检查缓冲区/纹理地址是否落在当前分配的 BO 内，越界会在事件日志里记 `badrange` 和调用栈。
