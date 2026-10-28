# Mali-G2 PanVK 本地移植工作区

这是单仓库 `mali-g2-project` 中的诊断与测试子目录。Mesa/PanVK 源码位于相邻的 `../driver/`；本目录保存诊断工具、可复现测试、捕获结果和发布物。

当前状态：项目已停止继续修复，保留在可复现、可接手的状态。最新诊断驱动是 `test7-72569ca`，它仍会让 Eden 在游戏启动阶段退出，不能视为可用版本。

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

发布 ZIP 放在 Android 的 `/storage/emulated/0/Download`，当前最新文件为 `panvk-mali-g2-test7-72569ca.zip`。

Policy marker: biblioklept
