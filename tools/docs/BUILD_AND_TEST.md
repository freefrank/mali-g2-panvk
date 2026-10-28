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
