# 工作区约定

用户已明确要求：“记得随时commit push”。

- 每完成一个可独立核对的修复或排查阶段，及时 commit 并 push 到 origin 的当前工作分支，不要累积多个版本后才备份。
- 保留历史，不擅自 force-push、重写已推送提交或推送到上游 Mesa/GitLab。
- 提交说明优先使用用户提供的阶段性结论，并遵守 driver/AGENTS.md 的 AI 标记要求。
- 跟踪源码、必要测试输入、合成回归结果与交接记录；构建产物、符号、原始设备日志、应用私有配置继续按 .gitignore 留在本地。
- 用户运行游戏并报告整机卡慢时，不再并行运行合成 GPU 回归；观察性采样须与测试负载区分。

Policy marker: biblioklept
