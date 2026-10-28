# Mali-G2 PanVK 单仓库工作区

本目录集中保存 Mali-G2-Ultra-NX（Valhall v15）Android PanVK 移植所需的源码、诊断工具、测试证据、发布物和外部参考。它是唯一的项目 Git 根目录，旧驱动与工具仓库的历史均已合并保留。

## 目录

| 路径 | 内容 |
| --- | --- |
| `driver/` | Mesa/PanVK 驱动源码，原 `/root/mali-panvk` |
| `tools/` | G2 诊断、测试、捕获与发布物，原 `/root/mali-g2-backend` |
| `references/eden/` | Eden Optimized 相关只读源码摘录 |
| `references/malibridge/` | Vulkan Headers 与 MaliBridge 外部参考，不纳入 Git |
| `toolchain/` | Meson、Android 依赖和本地编译工具，不纳入 Git |

当前驱动为 `test10-3b3577e`，已修复 32 顶点属性/绑定越界，等待 Eden 实测。技术状态见 [tools/docs/STATUS.md](tools/docs/STATUS.md)，构建方法见 [tools/docs/BUILD_AND_TEST.md](tools/docs/BUILD_AND_TEST.md)。

旧的 Meson/Ninja 构建缓存包含旧绝对路径，没有迁入。需要继续开发时应在 `driver/build-host` 和 `driver/build-android-arm` 重新配置构建。

## 工作区约定

- `/root` 下只保留本项目的 `mali-g2-project/`，不要重建旧的同级项目目录。
- 驱动构建输出只放在 `driver/build-*`。
- 诊断工具构建输出只放在 `tools/build/`，临时日志和 trace 只放在 `tools/work/`。
- 发布驱动保存在 `tools/artifacts/`；只有交付给 Android 应用的 ZIP 才复制到公共 Download。

Policy marker: biblioklept
