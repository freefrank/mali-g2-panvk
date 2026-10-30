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

test46 已应用，ANB 图像销毁等待的 29 项候选回归及 1 项旧版故障对照通过。用户先确认切回正常，随后仍退出；最新只读捕获中 8 次表面创建全部成功，崩溃是 Eden 将空交换链传入 Android 取图接口。用户要求只修驱动，保留 test46；应用层剩余退出与持续卡顿均未标记解决。1×、Mailbox、两种异步关闭与两侧诊断关闭保持。证据与验证状态见 [HANDOFF.md](tools/docs/HANDOFF.md)。

现有 `driver/build-host` 和 `driver/build-android-arm` 已可用于增量构建，命令见构建文档。

## 工作区约定

- `/root` 下只保留本项目的 `mali-g2-project/`，不要重建旧的同级项目目录。
- 驱动构建输出只放在 `driver/build-*`。
- 诊断工具构建输出只放在 `tools/build/`，临时日志和 trace 只放在 `tools/work/`。
- 发布驱动保存在 `tools/artifacts/`；只有交付给 Android 应用的 ZIP 才复制到公共 Download。

Policy marker: biblioklept
