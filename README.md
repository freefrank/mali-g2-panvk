# Mali-G2 PanVK 单仓库工作区

本目录集中保存 Mali-G2-Ultra-NX（Valhall v15）Android PanVK 移植所需的源码、诊断工具、测试证据、发布物和外部参考。它是唯一的项目 Git 根目录，旧驱动与工具仓库的历史均已合并保留。

## 目录

| 路径 | 内容 |
| --- | --- |
| `driver/` | Mesa/PanVK 驱动源码，原 `/root/mali-panvk` |
| `tools/` | G2 诊断、测试、捕获与发布物，原 `/root/mali-g2-backend` |
| `references/eden/` | Eden Optimized 相关只读源码摘录 |
| `references/eden-upstream/` | Eden 上游渲染器源码（不纳入 Git） |
| `references/malibridge/` | Vulkan Headers 与 MaliBridge 外部参考，不纳入 Git |
| `toolchain/` | Meson、Android 依赖和本地编译工具，不纳入 Git |

test45 初期明显顺滑，但游戏与通知栏仍会持续卡顿。标准入口下已再次复现切回原生崩溃；adb 日志与内核记录确认 GPU 写入了提前释放的 Android 交换链缓冲区，3,639 条生命周期事件零丢失。test46 候选为 G2/kbase 的 ANB 图像销毁增加完成等待，正在构建，尚未发布。保留 1×、Mailbox、两种异步关闭与两侧诊断关闭；证据与验证状态见 [HANDOFF.md](tools/docs/HANDOFF.md)。

现有 `driver/build-host` 和 `driver/build-android-arm` 已可用于增量构建，命令见构建文档。

## 工作区约定

- `/root` 下只保留本项目的 `mali-g2-project/`，不要重建旧的同级项目目录。
- 驱动构建输出只放在 `driver/build-*`。
- 诊断工具构建输出只放在 `tools/build/`，临时日志和 trace 只放在 `tools/work/`。
- 发布驱动保存在 `tools/artifacts/`；只有交付给 Android 应用的 ZIP 才复制到公共 Download。

Policy marker: biblioklept
