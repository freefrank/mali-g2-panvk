# Mali-G2 PanVK 本地移植工作区

这是单仓库 `mali-g2-project` 中的诊断与测试子目录。Mesa/PanVK 源码位于相邻的 `../driver/`；本目录保存诊断工具、可复现测试、捕获结果和发布物。

用户限定只修驱动，主线是 LOR 性能。接手先读 [开发交接](docs/HANDOFF.md) 和 [机器续接状态](continuation.json)。

## 导航

- [开发交接](docs/HANDOFF.md)：当前状态、约束、下一步，以及迁移到 Windows + adb 的方法。
- [归档](docs/archive/)：旧交接（逐日实验记录）和 test10 时期的历史记录，不是当前操作清单。
- [设备端脚本](device/README.md)：用 adb 在手机上执行的采样和 A/B 脚本。
- [构建与测试](docs/BUILD_AND_TEST.md)：环境、构建命令、独立 GPU 测试和 Eden 测试。
- [目录与数据](docs/DATA_LAYOUT.md)：目录职责、哪些文件可重建、哪些证据必须保留。
- `artifacts/`：各测试版本的解包驱动和元数据。
- `fixtures/`：版本化测试输入与结果。

## 单仓库关系

| 相对路径 | 用途 |
| --- | --- |
| `../driver/` | Mesa/PanVK 驱动源码 |
| `./` | 工具、测试、证据和文档 |
| `../references/eden/` | Eden 相关只读源码摘录 |
| `../toolchain/` | 本地构建工具与依赖 |

驱动包和未剥离符号在本地 `artifacts/`（test45–74），不在 Git 里。

Policy marker: biblioklept
