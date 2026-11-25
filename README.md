# Mali-G2 PanVK 单仓库工作区

本目录集中保存 Mali-G2-Ultra-NX（Valhall v15）Android PanVK 移植所需的源码、诊断工具、测试证据、发布物和外部参考。它是唯一的项目 Git 根目录，旧驱动与工具仓库的历史均已合并保留。

接手开发先读 [开发交接](tools/docs/HANDOFF.md) 和 [机器续接状态](tools/continuation.json)。用户限定只修驱动，主线是 LOR 性能（同场景约 15–25 FPS，原厂约 60 FPS）；推荐日常版 test68，最新包 test74。旧的交接和历史记录在 [tools/docs/archive/](tools/docs/archive/)。

## 目录

| 路径 | 内容 |
| --- | --- |
| `driver/` | Mesa/PanVK 驱动源码，原 `/root/mali-panvk` |
| `tools/` | G2 诊断、测试、捕获与发布物，原 `/root/mali-g2-backend` |
| `references/eden/` | Eden Optimized 相关只读源码摘录 |
| `references/eden-upstream/` | Eden 上游渲染器源码（不纳入 Git） |
| `references/malibridge/` | Vulkan Headers 与 MaliBridge 外部参考，不纳入 Git |
| `toolchain/` | 旧 PRoot 环境的 Meson、Android 依赖和编译工具，不纳入 Git，迁移后不再使用 |

开发环境正在从手机 PRoot 迁移到 Windows（WSL2 构建）+ adb，见 [HANDOFF.md §6](tools/docs/HANDOFF.md)。设备端脚本在 `tools/device/`。

## 工作区约定

- 驱动构建输出只放在 `driver/build-*`。
- 诊断工具构建输出只放在 `tools/build/`，临时日志和 trace 只放在 `tools/work/`。
- 发布驱动保存在 `tools/artifacts/`；只有交付给 Android 应用的 ZIP 才复制到公共 Download。

Policy marker: biblioklept
