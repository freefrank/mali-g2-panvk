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

用户确认 test44 仍卡顿；系统卡慢随游戏 FPS 下降而加重，下拉状态栏也会拉低游戏 FPS。test43、Mailbox 与调度 tick 对照均未改善。test44 去除已由命令缓冲完成的重复缓存清理，保留必要清理与完成同步，渲染和内存读回回归已通过；用户实测仍卡顿，继续排查 GPU 与系统合成之间的争用或等待。调度 tick 临时从 100 改为 10 毫秒也无改善，已恢复 100。分辨率保持 1×，异步 GPU 和两侧诊断保持关闭。当前包在 Download，同场景验证和交接信息见 [HANDOFF.md](tools/docs/HANDOFF.md)，构建命令见 [BUILD_AND_TEST.md](tools/docs/BUILD_AND_TEST.md)。

现有 `driver/build-host` 和 `driver/build-android-arm` 已可用于增量构建，命令见构建文档。

## 工作区约定

- `/root` 下只保留本项目的 `mali-g2-project/`，不要重建旧的同级项目目录。
- 驱动构建输出只放在 `driver/build-*`。
- 诊断工具构建输出只放在 `tools/build/`，临时日志和 trace 只放在 `tools/work/`。
- 发布驱动保存在 `tools/artifacts/`；只有交付给 Android 应用的 ZIP 才复制到公共 Download。

Policy marker: biblioklept
