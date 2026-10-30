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

用户确认 test45 初期明显顺滑，但几秒后游戏与通知栏仍卡。早先切回崩溃位于 Android 表面重建路径，现已关闭异步呈现。最新退到列表时进程和游戏线程仍在，发现重复列表页盖住原游戏页；已在用户存档退出后修正桌面任务入口，实际游戏切回待复测。adb 已配对并取得本机 root shell。31 项驱动回归与内核 4/3 读回仍有效，保持 test45、1×、Mailbox、异步 GPU 与两侧诊断关闭。当前包仍在 Download，证据见 [HANDOFF.md](tools/docs/HANDOFF.md)。

现有 `driver/build-host` 和 `driver/build-android-arm` 已可用于增量构建，命令见构建文档。

## 工作区约定

- `/root` 下只保留本项目的 `mali-g2-project/`，不要重建旧的同级项目目录。
- 驱动构建输出只放在 `driver/build-*`。
- 诊断工具构建输出只放在 `tools/build/`，临时日志和 trace 只放在 `tools/work/`。
- 发布驱动保存在 `tools/artifacts/`；只有交付给 Android 应用的 ZIP 才复制到公共 Download。

Policy marker: biblioklept
