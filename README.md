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

最新试版 test45 对齐 G2 计算/片元仲裁参数，已通过 31 项运行回归，内核读回确认 4/3，游戏流畅度待用户实测。test44 仍卡顿；此前内核记录显示系统合成 GPU fence 出现约 100ms 长等待，关闭模糊也未改善。分辨率保持 1×，异步 GPU 与两侧诊断关闭。测试包已放在 Download，源码与验证记录见 [HANDOFF.md](tools/docs/HANDOFF.md)，构建命令见 [BUILD_AND_TEST.md](tools/docs/BUILD_AND_TEST.md)。

现有 `driver/build-host` 和 `driver/build-android-arm` 已可用于增量构建，命令见构建文档。

## 工作区约定

- `/root` 下只保留本项目的 `mali-g2-project/`，不要重建旧的同级项目目录。
- 驱动构建输出只放在 `driver/build-*`。
- 诊断工具构建输出只放在 `tools/build/`，临时日志和 trace 只放在 `tools/work/`。
- 发布驱动保存在 `tools/artifacts/`；只有交付给 Android 应用的 ZIP 才复制到公共 Download。

Policy marker: biblioklept
