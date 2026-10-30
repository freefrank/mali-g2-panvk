# Mali-G2 PanVK 本地移植工作区

这是单仓库 `mali-g2-project` 中的诊断与测试子目录。Mesa/PanVK 源码位于相邻的 `../driver/`；本目录保存诊断工具、可复现测试、捕获结果和发布物。

当前状态：用户确认 test44 仍卡，系统卡慢随游戏 FPS 降低而加重，下拉状态栏也会拉低游戏 FPS。test44 消除命令缓冲已经完成清理后的重复缓存清理，保留必要同步，渲染、读回和提交压力回归已通过，用户实测仍卡顿。10 毫秒调度 tick 对照也无改善，已恢复原 100 毫秒。异步 GPU 与两侧诊断保持关闭，分辨率 1×。证据及下一步见 [交接文档](docs/HANDOFF.md)。

## 导航

- [当前状态](docs/STATUS.md)：设备、提交、已验证能力、未完成项和关键结论。
- [构建与测试](docs/BUILD_AND_TEST.md)：环境、构建命令、独立 GPU 测试和 Eden 测试。
- [目录与数据](docs/DATA_LAYOUT.md)：目录职责、哪些文件可重建、哪些证据必须保留。
- `artifacts/`：各测试版本的解包驱动和元数据。
- `captures/`：Eden 运行期间捕获的原始编译器输入。
- `fixtures/`：版本化测试输入与结果。

## 单仓库关系

| 相对路径 | 用途 |
| --- | --- |
| `../driver/` | Mesa/PanVK 驱动源码 |
| `./` | 工具、测试、证据和文档 |
| `../references/eden/` | Eden 相关只读源码摘录 |
| `../toolchain/` | 本地构建工具与依赖 |

测试 ZIP 放在 Android 的 `/storage/emulated/0/Download`。最新试版为 `panvk-mali-g2-test44-perf-bfcc5d7-e5c4f675.zip`（消除重复缓存清理，用户确认仍卡顿）；test43 为剩余卡顿未改善的对照，test42 为用户确认流畅很多但仍卡的对照。test41 为 55–58 FPS 但持续抖动，test39 为约 50 FPS，test36 为已确认成功的渲染修复基线。test40 的候选已撤回，没有发布包。这些包构建时基于 `bfcc5d7` 加未提交补丁；现已补入独立源码检查点，映射见 `fixtures/test44-source-checkpoints.json`。原包名与哈希保留，补丁和构建信息仍见本地 `artifacts/testN/source.patch`、`build.json`。

Policy marker: biblioklept
