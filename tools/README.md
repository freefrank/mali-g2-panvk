# Mali-G2 PanVK 本地移植工作区

这是单仓库 `mali-g2-project` 中的诊断与测试子目录。Mesa/PanVK 源码位于相邻的 `../driver/`；本目录保存诊断工具、可复现测试、捕获结果和发布物。

当前状态：用户确认 test45 初期明显改善，数秒后仍卡，回前台时闪退。实际日志指向 Android surface 创建失败后的呈现异常重抛；已备份并关闭独立的 async_presentation，保留 test45，等待用户复测。异步 GPU 与两侧诊断关闭，分辨率 1×。最新证据及下一步见 [交接文档](docs/HANDOFF.md)。

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

测试 ZIP 放在 Android 的 `/storage/emulated/0/Download`。最新试版为 `panvk-mali-g2-test45-perf-24e32e8-7d407064.zip`（G2 仲裁参数对齐，用户确认初期改善后仍卡）；test44 为用户确认仍卡顿的对照；test43 为剩余卡顿未改善的对照，test42 为用户确认流畅很多但仍卡的对照。test41 为 55–58 FPS 但持续抖动，test39 为约 50 FPS，test36 为已确认成功的渲染修复基线。test40 的候选已撤回，没有发布包。test45 已先提交源码再构建，记录在 `fixtures/test45-build.json`。test44 及更早的这些包构建时基于 `bfcc5d7` 加未提交补丁；现已补入独立源码检查点，映射见 `fixtures/test44-source-checkpoints.json`。原包名与哈希保留，补丁和构建信息仍见本地 `artifacts/testN/source.patch`、`build.json`。

Policy marker: biblioklept
