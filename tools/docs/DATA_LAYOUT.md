# 目录与数据约定

本文件中的相对路径以 `tools/` 为基准，整个仓库根目录为 `/root/mali-g2-project`。接手入口是 [HANDOFF.md](HANDOFF.md)，[STATUS.md](STATUS.md) 保存历史快照，[continuation.json](../continuation.json) 提供机器可读状态。

## 稳定内容

| 路径 | 内容 | Git 策略 |
| --- | --- | --- |
| 根目录 `*.c` / `*.py` | 诊断和测试源码 | 跟踪 |
| `docs/` | 状态、构建和数据说明 | 跟踪 |
| `isa/` | v14/v15 ISA 定义 | 跟踪 |
| `fixtures/` | 固定输入、基准输出、验证报告 | 选择性跟踪 |

## 大型或可重建内容

| 路径 | 内容 | Git 策略 |
| --- | --- | --- |
| `build/bin/` | 本机和 Android 辅助程序 | 忽略，可重建 |
| `build/obj/` | 中间对象 | 忽略，可重建 |
| `build/shaders/` | 生成的 SPIR-V | 忽略，可重建 |
| `build/generated/` | 代码生成器输出 | 忽略，可重建 |
| `work/logs/` | 构建和诊断日志 | 忽略，按需保留 |
| `work/traces/` | ioctl/Vulkan 临时 trace | 忽略，按需保留 |
| `work/research/` | 历史二进制与分析输出 | 忽略，保留但不作为发布物 |
| `references/` | 外部源码和 Android 参考实现 | 忽略，可重新获取 |
| `captures/` | 原始 Eden shader 捕获 | 忽略，不应删除 |
| `artifacts/` | 每个 test 版本的驱动与元数据 | 忽略，不应覆盖 |

## 发布规则

1. 驱动源码必须先提交，再构建，以保证 Mesa Git SHA 和 cache UUID 对应正确提交。
2. 新版本使用独立的 `artifacts/testN/` 或带 SHA 的版本目录，不覆盖旧版本；当前 test46 位于 `artifacts/test46/`。
3. ZIP 使用新的文件名并原子移动到 `/storage/emulated/0/Download`。
4. 发布前校验 ZIP CRC、库 SHA-256、成员列表和 `RUNPATH`。
5. 诊断版本必须明确标记，不能称为已修复版本。

## 保留原则

同一工作区接手时保留 `artifacts/`、`work/`、`captures/`、构建目录和工具链；这些被忽略的文件不会随 Git clone 或 push 传递。包、符号、配置备份和原始设备日志是否存在，要区分“同一工作区仍在”和“远端源码中可取得”。驱动二进制、符号以及具体证据的哈希以对应 `fixtures/testN-build.json` 等 manifest 为准。

整理只移动文件，不删除历史数据。`fixtures/` 中未跟踪的实验结果可能仍有研究价值；确认被稳定报告取代前，不批量清理。

本次整理把 41 个历史未跟踪 fixture 原样归档到 `work/research/fixtures/`。整理后的 `fixtures/` 含 214 个文件，全部已纳入 Git；根目录与 fixture 目录的 Git 状态均保持干净。

Policy marker: biblioklept
