# Mali-G2 PanVK 本地移植工作区

这是单仓库 `mali-g2-project` 中的诊断与测试子目录。Mesa/PanVK 源码位于相邻的 `../driver/`；本目录保存诊断工具、可复现测试、捕获结果和发布物。

当前交付为 test46：ANB 内存回收等待的 29 项候选回归及 1 项旧库预期故障对照通过，但用户游戏切回和持续卡顿仍未解决。最新退出是 Android loader 收到空交换链后的空指针访问；用户限定只修驱动。接手先读 [开发交接](docs/HANDOFF.md) 和 [机器续接状态](continuation.json)。

## 导航

- [开发交接](docs/HANDOFF.md)：当前 test46、范围限制、证据、Android 访问与下一步。
- [历史记录](docs/STATUS.md)：test10 状态与整理前交接快照；不是当前操作清单。
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

当前 ZIP 为 `/storage/emulated/0/Download/panvk-mali-g2-test46-resume-49bf70e-0f5c62e4.zip`，元数据与哈希见 `fixtures/test46-build.json`；未剥离符号保存在本地 `artifacts/symbols/test46-unstripped.so.zst`。这些二进制和原始日志未加入 Git。test45 的仲裁增量保留，旧版本试验与源码检查点见历史记录。

Policy marker: biblioklept
