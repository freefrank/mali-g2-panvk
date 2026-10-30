# 交接文档（2026-10-30，test45 仲裁参数候选已交付；游戏流畅度待实测）

接手前先读完本文件，再读 `STATUS.md`（历史结论）和 `BUILD_AND_TEST.md`（构建细节）。
Policy marker: biblioklept

## Git 检查点与推送约定

用户已要求“记得随时commit push”，已写入根目录 AGENTS.md。后续每个可独立核对的阶段及时提交并推送 origin 当前分支。此次从归档的完整源码补齐以下检查点，标题使用用户原话，附 Generated-by 标记；没有重写已推送历史。

| 源码版本 | Git 提交 |
| --- | --- |
| test36 | `665d426ac688` |
| test37 | `959fcb03620f` |
| test38 | `2698a0e78f71` |
| test39 | `c64494042fd4` |
| test41 | `3c4b27dac353` |
| test42 | `2b1a3e91f9e4` |
| test43 | `2f9eee9da902` |
| test44 | `692b8bb913aa` |

源码提交、driver 子树、原包库/ZIP/补丁哈希的机器可读映射在 `tools/fixtures/test44-source-checkpoints.json`。原包构建发生在这些提交之前，包名中的 bfcc5d7 与历史 manifest 不改写；其 driver 源码已逐字节对应到表中提交。旧记录中的“无提交或推送”描述当时状态，已由本节补充。构建产物、符号、原始设备日志和私有配置未加入 Git。

## 当前交付：test45，G2 计算/片元仲裁候选

- 用户反馈关闭窗口模糊后仍「卡」，并要求继续。此前模糊开关已经自动恢复；不把补采空闲窗口当作性能 A/B 证据。test44 及前述系统卡慢结论仍成立。
- test45 源码先提交并推送到 `24e32e8b86164c9375aebab92a037b487f8ed872`，再构建；包内 MESA_GIT_SHA1 与之对应。相对 test44 只改 kbase_kmod.c：给已有 112 字节兼容请求中的计算仲裁字段命名，并为架构 15 设置 threshold=min(max_compute,4)、ratio=3。其他架构保留全零。全局队列优先级、核心掩码/数量、缓存清理、fence 等待和所有渲染修复均未改动，诊断默认仍关闭。
- 原厂队列此前实测为 4/3，PanVK 为 0/0；这只是候选依据，不能声称差异就是卡顿根因。已核对实际设备 mali_kbase.ko 的 `kbasep_cs_queue_group_create_1_35` 转换：输入偏移 48/49 到当前请求偏移 56/57；编译断言保持总长 112。公开 dijun r49 头文件存在不同 ABI，不能直接照搬。证据 `fixtures/test45-abi-contract.json`，实际内核二进制仅保留本地。
- 用户已存档并完全退出 Eden，native-root 确认没有游戏 GPU 线程后才执行 GPU 回归。最终 stripped 包库通过 31 项运行回归：Android 图形/AHB 9、提交与 native fence/导入/MSAA/颜色/压缩纹理/深度 20、默认/显式关闭诊断审计 2。20 项中包括 4096 次独立 native fence 提交，校验复制结果，FD 9→9。独立回归中的计时不是游戏 FPS 证明。
- 另有 1 项原生内核读回：短提交运行期间对准确测试 PID 读取只读 debugfs，确认 C_EP PRI Threshold=4、Ratio=3，Priority=2 未变。最初空闲设备快照没有驻留组，随后上下文快照仅能确认挂起队列，均不计作参数读回成功；最终活动队列快照才作为证据。再加 ELF 全部 27 个 SHF_ALLOC 节一致、ZIP CRC/库 SHA、Download 副本 SHA 等 3 项文件检查，共 35 项。
- 包：`/storage/emulated/0/Download/panvk-mali-g2-test45-perf-24e32e8-7d407064.zip`。库 SHA `7d407064272ac63efa11d8958273da6d8e8cffc327b7bdf6cf8ff5efc827450e`；ZIP SHA `d029530c19deb0e1c2a9f9a7bec9fa49f0128a1746c4b16d2b06d92bab11b60e`。构建、报告及符号哈希在 `fixtures/test45-build.json` 和本地 `artifacts/test45/build.json`；符号 `artifacts/symbols/test45-unstripped.so.zst`。原包 test44 保留。
- 已确认测试进程结束、私有 debugfs 挂载移除；调度 tick=100，窗口模糊设置=null/启用，两侧诊断关闭。没有改动 Eden 私有配置或强行覆盖运行中的驱动。下一步由用户导入 test45 并重启 Eden，在同一位置比较移动、FPS 和下拉通知栏。保持 1×、异步 GPU 关闭、Mailbox 当前值；游戏改善尚未验证，不标记为已解决。

## test44 排查：通知栏、SurfaceFlinger 与 HWC 同一时间线

- 本轮通过运行中 map_files 校验，当前实际库为 test44 `e5c4f675...`。没有新驱动改动或 test45 发布；旧 test43 映射仍可见，不能只按第一条映射判断当前版本。
- 用户配合反复下拉、收起通知栏。首次 7.975 秒观察性采样记录 36,209 样本；确认配合操作后的联合 7.978 秒采样记录 30,373 样本，两次均零丢失。联合覆盖游戏、SystemUI、SurfaceFlinger 和厂商 HWC 所有线程，调用链错误约 4.29%。没有并行合成 GPU 负载，也没有修改驱动、Eden 配置或调度参数。
- 联合记录按上下文切换配对：SF 的 presentDisplay 等待 122 段，p95 47.91ms、最大 80.07ms；HWC 的 Fence::wait 同为 122 段，p95 47.59ms、最大 79.80ms；commitDSI-0 在 drmModeAtomicCommit 中的离 CPU 区间 p95 37.63ms、最大 74.04ms。最长一次 SF、HWC、DRM 等待的起止时间嵌套吻合。厂商库本地反汇编与相邻超时日志表明，HWC 的这个 Fence::wait 等上一帧 present 完成，不能直接称为等待新游戏图层的 acquire fence。
- 同窗通知栏新增 119 帧；有效完成时间 115 帧，IntendedVsync→FrameCompleted p50 38.04ms、p95 94.10ms、最大 131.92ms；dequeueBuffer p95 47.88ms、最大 115.38ms。这说明系统卡慢确实进入合成/显示等待链；仍需内核 fence 身份才能分清游戏 GPU、系统合成 GPU、显示控制器各自责任，不能宣布根因已修复。
- 线程离 CPU 区间含阻塞和就绪调度延迟，不是纯 GPU 时间；累计多线程百分比不能当作单帧占比。gfxinfo 是最多 120 帧滚动窗口，按新 IntendedVsync 去重，不用 Flags=0 过滤通知栏帧。TimeStats 只取前后增量，其 fence 延迟回填也不能当作精确 8 秒 FPS。
- 用户再次配合后补采 8.148 秒内核显示与 dma_fence 事件：6,916 样本、零丢失。按 TID/context/seqno 配对 wait start/end，保留开始时的驱动/时间线（结束时 fence 可能已 detached）：显示提交线程等待 SF GPU 时间线 170 段、总 4965.04ms，p50 24.86ms、p95 75.42ms、最大 99.90ms；等待游戏 GPU 时间线 32 段、总 424.78ms，p95 30.09ms、最大 52.37ms。因此该操作窗口的主要迟到信号来自 SurfaceFlinger GPU，而不应直接归为游戏 present fence；仍需区分 SF 自己的 GPU 工作、其依赖和 GPU 调度。没有提前修改 fence 等待或序列化游戏呈现。
- 已核对 SF 实际加载原厂 vulkan.mali.so/libGLES_mali.so。其他常驻低优先级 GPU 上下文属于 Termux UI；快照中的端点分配不是合成 GPU 测试仍运行的证据。补充只读 debugfs 快照结束后挂载已移除，没有改优先级或 tick。
- 做过有自动恢复的窗口模糊对照：10:04:39Z 将 global disable_window_blurs 从 null 临时设为 1，系统查询 Blur enabled=false；10:07:41Z 自动删除该临时项，读回 null 且 Blur enabled=true。用户后续反馈仍「卡」。关闭时补采 8.152 秒、11,350 样本、零丢失，但没有新增 SystemUI 帧，不能拿这段空闲窗口与操作通知栏的窗口比较并宣称模糊开关有效或无效。游戏、驱动和频率设置未变；不要把这次对照写成永久关闭模糊或已解决卡顿。
- 摘要与本地原始数据哈希在 `tools/fixtures/test44-system-composition-profile.json`；完整原始记录、分析脚本与厂商库反汇编留在忽略目录 `tools/work/eden-perf45/`。单独的 HWC 记录和后续内核栈快照没有覆盖相同操作时段，不能用空闲比例否定联合记录中的等待。下一步核对系统合成效果与依赖/调度，确认后再决定驱动改动。

## test44 历史交付：用户确认卡顿依旧

- 最新用户反馈优先：test44「卡顿依旧」。用户进一步观察到系统界面卡慢随游戏 FPS 变化：游戏 60 FPS 时系统不卡，游戏低帧时系统更卡；下拉状态栏会拉低游戏 FPS，随之整个 Android 界面都卡慢。此前 test43、Mailbox 和 10ms 调度 tick 均无改善；本地 GPU 回归结束后系统仍慢。下一步应区分 GPU 资源争用与系统合成等待，不能据此直接宣布某一根因，也不能继续把 test44 标为待用户验证或已修复。
- 上一轮运行中的库 SHA 校验为 test43 `c1e421b0...`；本轮已经确认 test44，见上节。首个 TimeStats 窗口因错误地用 dump 是否为空判断 enabled 而没有实际启用，混入旧统计，已排除其呈现间隔。正确启用后的新窗口按前后增量解析，同一 SurfaceView 60 Hz 分组新增 724 帧：16ms×421、33ms×281、50ms×22；该新图层累计平均 42.317 FPS。这是实际呈现统计，不等于游戏内部 FPS。解析器 `tools/work/eden-perf44/analyze_intervals.py`，已核对相同文件差分为零和 724 帧直方图。
- 标准刷新率接口做过有恢复措施的短对照：min_refresh_rate 临时 120、非持久 preferred mode 临时 120/120.00001。精确值可匹配 preferred mode1，但实际面板一直 mode4/60 Hz，所以没有成功的 120 Hz 对照，不能据此声称刷新率无关或锁 120 有效。min 已恢复 null，preferred 已恢复 -1/-1/0。root 下 cmd/settings 直接把 stdout 指向共享存储会 Failed transaction；经管道到 cat 可正常工作。证据 `refresh-aba/`、`exact-refresh/`、`cmd-probe/`。
- 用户明确存档并完全退出后，已备份并只改 Eden 两行：use_vsync=2→1、default=true→false。官方 settings_enums.h 已获取，实际旧日志也确认 Fifo；重启日志确认 Mailbox、Res1X、异步 GPU=false、debug=false、GPU log=Off。游戏自定义配置逐字节未变。备份 `config.ini.panvk-vsync44-20261030T084142Z.bak`，diff/哈希 `config-result.json`、`config-applied.diff`，原始副本在 `config-applied/`。用户确认无改善；当前仍为 Mailbox，未在游戏运行中强改回去，后续对照应记录这个状态。
- 用户报告整机卡慢时本地回归已结束，没有再运行任何合成 GPU 测试。只读资源采样中 CPU 约 70% idle、MemAvailable 约 3.9 GB，memory/IO PSI 很低，2 秒 pswpin=12、pswpout=0；不能用这一个窗口排除所有 CPU/内存问题。系统有 100ms GPU scheduling tick；临时改为 10ms、读回确认后用户仍报告卡慢，已恢复并多次读回 100。仅为诊断，没有写入永久配置。原始 `tick/`、`scheduler-params/`、`resources/`。
- 临时在私有目录只读挂载 debugfs 取得实际 CSF 快照，随后卸载并删除挂载点。PanVK 三队列组 Priority=2（low），SurfaceFlinger PID18707 的组 Priority=0（high）；没有发现游戏意外最高/实时优先级的证据，未改队列优先级。原始 `debugfs/`。快照不是完整抢占时间序列，不能据此宣布 GPU 调度正常或卡慢已修复。
- test44 的驱动增量只涉及 kbase_kmod.c/.h 和 csf/panvk_vX_gpu_queue.c：finish_cs 本已在每个子队列等待全部 scoreboard，并完成 LSC/L2 clean 及 flush 等待，提交环又重复 clean。为已满足该契约的 G2 主命令流显式标记 cache_clean；只有关闭诊断且没有 u_trace points 时启用，按该子队列最后一次 CALL 状态省去重复环末 clean。入口 invalidate、命令流内 finish clean、原始初始化/未标记流、空提交 clean、错误屏障和所有完成序号均保留；其他架构保留旧路径。
- 实际映射环读回验证：同一短测试三条环各有 42 个结束标记，环内 CACHE_FLUSH2 数从 250 降至 130；命令流内部必要 clean 不在环扫描中、仍保留。诊断开启对照为 250→250。最初仅跳过空队列的方案只降到 247，已由最终方案取代；LD_PRELOAD 截获方案没有载入，未计为通过证据。最终扫描直接读测试程序自己的 64KB /dev/mali0 映射，并要求反复出现精确的 ERROR_BARRIER/MOVE/MOVE/SYNC_ADD64/WAIT 序列。
- 回归全部通过：命令环对照 4、提交 ABBA 正确性/计时 16、批量/空提交压力 10（含 128 次独立提交后逐次插空提交，全部 fence 与复制段验证，FD 9→9）、native fence 12、Android 图形 9、Eden 计算 9、诊断审计 4、纹理 32、MSAA 48、深度/模板往返 26，共 170 项。ABBA 计时有波动，部分大批次改善，不宣称统一提升或游戏已 60 FPS。
- 用户报告整机卡慢后未追加包内 GPU smoke。改为只读验证 stripped/unstripped 的全部 27 个 SHF_ALLOC 节：字节、地址、大小、类型及标志相同，包含代码、常量、数据、动态符号/重定位表；再验 ZIP CRC/库哈希和 Download 副本，共 3 项。总计 173 项，报告 `tools/fixtures/test44-*.json`，清单区分了运行测试与文件检查。
- 包：`/storage/emulated/0/Download/panvk-mali-g2-test44-perf-bfcc5d7-e5c4f675.zip`；库 SHA `e5c4f67565560404b37a50c334680046ea94341db4f1d62f3dd0f16208206fec`，ZIP SHA `02985a9c42a0aa70a5b1024f8707b75c435f38e5a800ce0a933b07e738dedd71`。全源码、相对 test43 增量与报告哈希 `tools/artifacts/test44/build.json`，符号 `tools/artifacts/symbols/test44-unstripped.so.zst`。无提交或推送。
- 下一步：存档退出并导入 test44，重启后在相同位置对比游戏移动及通知栏/系统界面。分辨率 1×，异步 GPU 与两侧诊断继续关闭，Mailbox 目前仍为 1。test44 用户确认仍卡；下一步应继续读实际系统合成等待与 GPU 争用，游戏运行时不再并行合成 GPU 回归。不要声称缓存候选已定位/解决整机卡慢或异步崩溃。

## test43 完成线程唤醒：用户确认卡顿依旧

- 用户确认 test42「好很多，但还是卡」，并回复已在游戏中持续移动。test42 的游戏改善已有用户确认，剩余卡顿仍未解决，不能记为效果未知或已经流畅 60 FPS。
- 本轮从进程 `map_files` 复制的库 SHA 与 test42 的 `3ae37ef3...` 完全相同。有效 CPU 7.986 秒、1,726 样本，四线程 off-CPU 7.995 秒、记录 27,442 样本，均零丢失。GPU 利用率 92/95/99%、频率 1.248 GHz，DDR 4.2/4.2/5.472 GHz。呈现线程主要等帧；GPU 和 GPUFencing 仍在 MasterSemaphore 等待。符号化时当前 ELF 的 simpleperf 相对可执行映射偏移应加 `0xad6000`，不能直接将该偏移作为 ELF 虚拟地址解释。
- 用户确认移动后补采了进程所有线程及 SurfaceFlinger TimeStats。临时启用前 dump 为空，结束后已禁用；没有同时执行本地 GPU 回归。匹配游戏 SurfaceView 的 120 Hz 分组为 438 帧、平均 57.784 FPS：呈现间隔桶为 8ms×28、16ms×392、24ms×6、25ms×2、33ms×4、50ms×2、58ms×1、200ms×3。60 Hz 分组为 50 帧、47.847 FPS：16ms×37、24ms×1、33ms×11、66ms×1。采样有两次刷新率切换。这里是量化直方图，不是精确逐帧序列；SurfaceView 的 Timeline 帧数为零，不能将 jankyFrames=0 当作无卡顿证据。未修改刷新率或频率策略。采样及摘要在 `tools/work/eden-perf43/{active-capture,timing-capture,profile-summary.json}`，所有采样已结束。
- test43 相对 test42 只改 `kbase_kmod.c`：每次提交保存已有 KCPU native fence 的副本，完成线程 poll 每组最早未完成提交的 fence；用 eventfd 在新提交、fence 附加与退出时唤醒线程。仍读取各相关子队列序号，确认全部达到目标后才回调；原设备错误通知与超时兜底保持。FD 副本在完成时关闭，poll 的临时副本在每轮关闭，eventfd 随设备释放。没有新增 KCPU fence 提交，也没有跳过 GPU 等待。
- 新 `vulkan_completion.c` 从同次提交的独立 semaphore 导出 native SYNC_FD，观察其触发后再等普通 VkFence。12 项交替对照均校验复制结果，覆盖连续提交和 2/16ms 间隔。连续小提交的 CPU 总时延较低，但 native→host 的尾部和有间隔场景结果有波动，**没有统一降低尾延迟或游戏卡顿已修复的结论**；该计时还包含并发 Eden 调度。数据 `test43-completion.json`。最初 acquire runner 沿用了 test41 的阻塞预期，误将 test42 的正确立即返回标为失败；已修正两版本都应保留 pending 的预期，最终 12 项全部通过。
- 新增 1/8/64/128 个独立 QueueSubmit 的压力组合，每组 32 轮加 8 轮预热，逐个等待 fence、核对每段复制结果，FD 数始终 9→9。native fence 六模式 × test42/候选 12、Android 图形 9、Eden 计算 9、诊断审计 4、压缩纹理 32、MSAA 48 均通过。最终 ZIP 提取库另测 native fence 3、压缩纹理 4、128 次提交压力 1，共 8 项。与 12 项计时正确性及 4 项压力组合合计 138 项；报告 `tools/fixtures/test43-*.json`。默认/显式关闭诊断仍全部零输出。
- 包：`/storage/emulated/0/Download/panvk-mali-g2-test43-perf-bfcc5d7-c1e421b0.zip`；完整哈希、源码和相对 test42 增量在 `tools/artifacts/test43/build.json`，符号 `tools/artifacts/symbols/test43-unstripped.so.zst`。保留 test42 对照；没有提交或推送。test32/test36 修复、颜色及受限深度 AFBC、test41 平铺、test42 acquire、能力位和诊断关闭都保持。
- 用户已确认卡顿依旧；后续 Mailbox、调度 tick 对照也无效，整机卡慢与 test44 候选见上方。异步 GPU 与两侧诊断保持关闭，不可删除 capture-layer 资源等待或宣称异步崩溃根因已修复。

## test42 native fence：用户确认流畅很多，但仍卡顿

- 用户已实测 test41：1× 约 55–58 FPS，但移动持续抖动。随后回复已回到游戏持续移动。当前目标是改善流畅度；不能继续将 test41 记为 FPS 未验证，也不能声称已达稳定 60。
- 已校验运行中的驱动与 test41 库 SHA 相同。首次 CPU 采样时没有游戏 GPU 线程，排除。随后按实际 GPU 线程启动采样：CPU 7.99 秒、635 样本、零丢失；off-CPU 记录 81,470 样本、零丢失。内容含 VP8/视频活动，不能当作同一室内稳定移动的逐帧分布。GPU 利用率 99%、频率 1.248 GHz、DDR 5.472 GHz。原始结果在 `tools/work/eden-perf42/active-capture/`。
- 补采实际名为 `VulkanPresent` 的呈现线程等四线程，5.98 秒、26,011 样本、零丢失。呈现线程主要在条件变量等待帧；GPU 与 GPUFencing 等待 MasterSemaphore。capture layer 的资源复用等待保留，不可直接删去。多线程 off-CPU 比例不是游戏帧耗时占比，也不能证明 acquire 阻塞就是本轮抖动的主因。SurfaceFlinger latency 只返回刷新周期，没有逐帧时间戳。结果在 `present-capture/`，摘要 `profile-summary.json`；全部采样已结束，GPU 回归在采样结束后执行。
- test42 相对 test41 只有 `panvk_android.c` 改变：有 semaphore 或 fence 时取消前置 CPU poll，将原始 native fence 交给已有 `vk_common_AcquireImageANDROID` / kbase sync-file 导入路径。完成对象在 native fence 触发前仍保持未就绪，后续 GPU 提交仍等待依赖。没有同步对象时保留原阻塞回退；已触发与 -1 路径保持。没有改动异步 GPU、Eden 配置、频率、能力位或现有渲染修复。
- 新增 `vulkan_acquire.c` 和 `acquire_delay.comp`：两个 logical device 各一个队列，使用真实 GPU 计算导出的 SYNC_FD，检查导入 fence、导出回读的 semaphore、后续消费者输出和生产者计算结果。test41/候选各测 semaphore/fence/both/none/signaled/minusone，共 12 项通过。人为 GPU 延迟约 386 毫秒时，test41 获取调用阻塞约 386 毫秒，候选三种有同步对象的调用约 0.02–0.06 毫秒返回且信号仍未就绪。该数字不代表游戏帧停顿；测试是同步导入入口验证，不是真实窗口交换链测试。构建与复现见 `BUILD_AND_TEST.md`。
- 其余回归：Android 图形/导入 9、Eden 计算 9、诊断审计 4、压缩纹理 32 全通过；从最终 ZIP 提取的 stripped 库另测三种延迟 native fence 和四种带 mip/layer/copy/LOAD 的压缩纹理，共 7 项通过。报告 `tools/fixtures/test42-*.json`，本轮共 73 项（含 test41 对照）；默认和显式关闭诊断时 socket、信号 handler、datagram、文件字节仍为零。
- 包：`/storage/emulated/0/Download/panvk-mali-g2-test42-perf-bfcc5d7-3ae37ef3.zip`。完整 SHA、源码补丁、相对 test41 的唯一增量及报告哈希在 `tools/artifacts/test42/build.json`；符号 `tools/artifacts/symbols/test42-unstripped.so.zst`。ZIP CRC 与包内库、交付副本均校验。保留 test41 对照，无提交或推送。
- 用户已确认流畅很多，但仍卡顿，后续采样和 test43 见上方。异步 GPU 和两侧诊断继续关闭，不能宣称修复了全部卡顿或异步闪退。

## test41 压缩纹理平铺：用户确认 55–58 FPS，移动持续抖动

- 用户确认 test39 在同一室内 1× 约 50 FPS，0.5× 基本稳定 60、偶尔掉帧，要求继续。该对照支持优先排查随像素量增长的片元/纹理开销，不能称为唯一瓶颈；最终目标仍是 1×。
- test41 为 G2 原生 4×4 压缩块、2D optimal 图像恢复 U-interleaved 布局。应用显式要求 BLOCK_TEXEL_VIEW_COMPATIBLE、1D/3D、非 4×4 块或 BC 软件模拟格式保留原线性回退。没有包含失败的 64K 平铺或缓存合并试验；test32/test36 渲染修复、颜色和受限深度 AFBC、256 工作组上限、能力位及诊断关闭均保持。
- 修复真正的复制寻址：压缩 U tile 是 4×4 个块，普通格式视图的硬件 tile 是 16×16 个 texel，不能直接重解释。内部 raw block 视图改为线性行别名；共用 vk_meta 的图像/缓冲复制着色器按 U 曲线（X xor Y）转换坐标，保留 mip、层和偏移。原生压缩采样描述符仍描述真实 U 布局。复制 shader key 增加布局标志，覆盖计算与图形复制源。
- **确认实际平铺**：同步 GPU 写入后逐块检查映射的图像内存，候选四帧全部符合独立 CPU U 曲线，test39 全部符合线性布局；按另一种布局检查分别有 8483/8482 个块不同。报告 `test41-layout-proof.json`。这是本驱动实现的布局检查，不是可移植 Vulkan 布局承诺。
- BC 测试块已改为随位置/帧变化的颜色，避免旧双色格掩盖地址置换；先在原厂与 test39 共 8 例验证严格 oracle，再测候选。基础四格式连续 8 帧均正确；32 例扩展覆盖 BC1/BC3/BC7/ASTC4×4 的小图边界、720p/1440p、mip2+layer2、分段上传、图像复制、线性↔平铺复制及显式 block-view 线性回退。BC1 经 RGBA16 AFBC 中间图像往返复制，原厂/test39/候选 × layer0/2 共 6 例通过，确认候选图形复制源也正确。
- 其他回归：Android 图形 9、Eden 计算 9、MSAA 48（24 候选、24 原厂）、深度/模板 roundtrip 扩展 26、诊断审计 4、stripped 交付库组合 4 全部通过。默认/显式关闭仍为零 UDP socket、信号 handler、datagram 和文件字节。报告在 `tools/fixtures/test41-*.json`，清单合计 168 个验证用例（含对照与计时用例的正确性检查）。
- GPU timestamp 计时将采样与片元绘制分开；同一库用显式 block-view 标记强制线性作 ABBA 对照。结果有波动、格式间也不一致，**不宣称统一加速比例或游戏已达 60 FPS**。完整数据 `test41-texture-timing.json`。后续用户实测 1× 约 55–58 FPS，但移动持续抖动，排查见上方。
- 测试包：`/storage/emulated/0/Download/panvk-mali-g2-test41-perf-bfcc5d7-a0fbf1ea.zip`；ZIP SHA-256 `92de31131b17a89e3d32f83728fccdc46dd655e8b0133c256d38e6d06e0b8d8d`；库 SHA-256 `a0fbf1ea818e854ead1c86b4abc2030326407587a18bfa6943ed7cb9846394c1`。完整源码、相对 test39 增量、构建清单在 `tools/artifacts/test41/`；符号在 `tools/artifacts/symbols/test41-unstripped.so.zst`。无提交或推送。
- 保留 test41 作为下一轮同场景比较基线；异步 GPU 和两侧诊断继续关闭。本轮未修改应用配置或频率。

## test40 排查记录：候选均撤回，没有交付包

- 用户反馈 test39 约 50 FPS，8 Gen 3 可达 60。已从当前进程 `map_files` 再次校验加载库 SHA，与 test39 完全一致。确认前台后的 7.98 秒 CPU 采样共 938 个、零丢失；GPU 利用率 97/94/95，GPU 频率 1.248 GHz，DDR 5.472 GHz。两侧诊断和异步 GPU 仍关闭，应用 GPU 日志保持 2055225961 字节。本轮未修改应用配置或频率。
- 成功读取 Kbase 1.39 `kinstr_prfcnt` 硬件计数器，16 个 shader core block、8 个 L2 block。使用 [Arm libGPUCounters 2.5.0 的 G2 布局](https://github.com/ARM-software/libGPUCounters/blob/378c7b59e2dae87cbbffb0ac9cf96ed13c939823/specification/database/hardwarelayout/Mali%20G2.xml)，没有套用旧 G1/G725 的计数器位置。只读源码和许可在 `tools/references/libGPUCounters-2.5.0/`。
- 有效计数器窗口没有同时运行本地 GPU 回归：缓存清理周期约占 GPU active 的 6.1–6.2%；fragment 周期约占 shader core active 的 95%；texture pending fetch 周期约为 fragment active 的 63%；四个取指单元归一化后的指令缺粮约 71%。这些类别重叠，不能相加当作帧耗时，也不能直接证明线性 BC 是唯一原因。全部 16 核的工作量相近，没有只用一半核心的证据。高 late-ZS kill 已记录，但没有据此强制 early-Z 或破坏 discard/深度语义。
- 入口缓存合并试验：同一提交、同一 subqueue 只在第一次 CALL 前清理失效。新增 `tools/vulkan_submit.c` 用跨命令缓冲复制链、逐轮更新 CPU 输入、每段回读核对及 GPU timestamp 比较 1/2/8/32 个命令缓冲。全部正确性检查通过；较大批次有改善，但常见双命令缓冲计时不稳定。图形 9、计算 9、MSAA 48、诊断审计 4 也通过，报告前缀 `test40-`；这些是已撤回候选的报告，不代表 test40 已交付。候选库和增量在 `tools/work/eden-test40/flush-batch-*`、`flush-batch.patch`。
- 压缩纹理平铺继续排查：64K 原格式 clump/尺寸、U tiling、4/16 block tile 和 raw interleave 组合仍有采样错误，全部撤回。第一次 U tiling PASS 实际受自动添加的 BLOCK_TEXEL_VIEW_COMPATIBLE 标记限制而退回 LINEAR，不能据此宣称恢复平铺；跳过限制后 raw 往返可相同但压缩采样失败。`vulkan_tiles` 新增可选 `MALI_G2_TILES_DUMP_RAW`，记录源和回读原始块，帮助区分未写入与寻址差异。
- **没有发布 test40 包**；驱动源码已逐字节恢复到 `tools/artifacts/test39/source.patch`，交付仍为 test39。试验、回退及结果索引在 `tools/work/eden-test40/investigation.json`；有效采样在 `tools/work/eden-perf40/active-capture/`、`counters-game.json`、`counters-named.json`、`counters-percore.txt`。首次等待采样在前台确认之前、`counters-raw.json` 和回归重叠，不能用于游戏帧耗时分析。所有采样已结束。
- 后续用户确认同一室内 0.5× 基本稳定 60、偶尔掉帧；没有将降低分辨率作为最终修复，后续 test41 见上方。

## test39 深度 AFBC：已确认同一室内约 50 FPS

- 用户确认 test38 在同一室内位置约 45 FPS，并保持游戏前台。通过原生 root 从 `/proc/PID/map_files` 复制完整加载库，SHA-256 与 test38 包完全一致；不要继续将这次 45 FPS 记为版本不明或待验证。
- 实际活动场景采样 7.99 秒、723 个用户态 CPU 样本、零丢失；GPU 利用率 94、94、93，GPU 频率 1.1136/1.248/1.248 GHz。没有 shader/带宽硬件计数器，不能直接断言带宽瓶颈。首次暂停/后台采样不用于判断 45 FPS。采样及摘要在 `tools/work/eden-perf39/active-capture/`、`profile-summary.json`，采样进程均已结束。
- test39 相对 test38 的唯一驱动增量：G2 单 mip、单 layer 深度/模板图像恢复正常 AFBC 选择；其余深度图像保留无压缩回退，普通能力检查仍排除 MSAA AFBC。颜色 AFBC、压缩纹理线性回退、test32/test36 修复、能力位、256 工作组上限保持。两侧诊断与 Eden 异步 GPU 继续关闭，本轮未修改应用设置或频率。
- 压缩纹理 64K 平铺尝试仍有原始块/采样错误，禁用 shader cache 也失败，已完整撤回。`pan_texture.c` 与 test38 逐字节一致；失败代码和测试证据保存在 `tools/work/eden-test39/*64k*`、`combined-results.json`，不能将该尝试装入交付包。
- 深度/模板往返复制对照 12/12（原厂、test38、候选，各四种格式）通过；候选扩展 26/26，覆盖 D16/D32/D24S8/D32S8、LOAD、partial render、discard、反向/拒绝深度、720p/1440p。回读原值后先将图像清为 depth=0.125、stencil=0xc3，再上传原值并逐像素校验，避免未执行上传的假通过；压缩属性查询确认候选实际使用 AFBC。
- 回归通过：压缩纹理 BC1/BC3/BC7/ASTC 四例含 mip/layer/copy/LOAD；Android 图形 9、Eden 计算 9、MSAA 48（24 当前、24 原厂）、诊断审计 4、交付库组合复核 1。默认/显式关闭的 socket、信号 handler、datagram、文件字节均为零。报告为 `tools/fixtures/test39-*.json`。
- 测试包：`/storage/emulated/0/Download/panvk-mali-g2-test39-perf-bfcc5d7-527fb2e6.zip`；ZIP SHA-256 `5d6a37ce811f7a2514133426792a3243e4a57d30da02cbad6599281670526a9f`；库 SHA-256 `527fb2e61332a1b7ad1dcb99c74bb0138755d6645c990bcf383b791406096fb6`。源码补丁、相对 test38 增量、清单在 `tools/artifacts/test39/`；符号在 `tools/artifacts/symbols/test39-unstripped.so.zst`。
- 用户确认 test39 约 50 FPS，仍未达到 60 FPS；加载库也已验证，后续排查见上方。保持异步 GPU 关闭。

## test38 颜色 AFBC 性能试版：用户确认同一室内约 45 FPS

- 用户确认 **关闭 Eden“异步 GPU 模拟”后不再闪退**，但室内持续 35–45 FPS、战斗约 50 FPS，目标 60 FPS；用户称更弱的 Adreno 在相同游戏可跑满 60。此对照支持并发线索，不能声称异步根因已修复；保持异步 GPU 关闭。
- 已按用户“存档并完全退出 Eden”的确认，备份并修改应用配置：`Renderer.debug=false`，`Debugging.gpu_log_level=0`，`gpu_log_vulkan_calls/gpu_log_shader_dumps/gpu_log_memory_tracking/gpu_log_driver_debug=false`，对应 default 标记显式覆盖。仅六个诊断设置及三个 default 标记改变，未改驱动选择、分辨率或同步模式。应用目录备份：`config/config.ini.panvk-perf38-20261029T233342Z.bak`。修改、diff、SHA、回读校验在 `tools/work/eden-perf38/`。用户反馈有改善但仍跑不满。
- 新进程 PID 27070 的 maps 无 `libVkLayer_khronos_validation.so`，GPU 日志在采样前后保持 2055225961 字节，确认应用诊断已关闭。8 秒用户态 CPU 采样 743 个样本；随后 8 秒等待采样 52537 个样本、零丢失，均已结束。采样中主 GPU 线程经 `RenderAppletCaptureLayer -> Layer::ConfigureDraw -> MasterSemaphore::Wait` 等待 GPU；等待不能直接省略，涉及资源复用。CPU 样本不等于帧耗时占比。
- 内核 `gpufreq_core/dynamic/util` 连续五次是 99、98、98、99、98；polling_history 也接近满载。仅依据该内核指标，未取得硬件 shader/带宽计数器。`gpufreq_usage` 实际是频率驻留表，不是利用率。SurfaceFlinger 的提交速率接近 60，不代表模拟器内部游戏 FPS，采用用户报告的 35–45/50 FPS。
- **test38 相对 test37 的唯一驱动改动**：`panvk_image_can_use_mod` 将 G2 的全面禁止 AFBC 缩小为仅禁止深度/模板 AFBC，恢复硬件支持的颜色 AFBC。压缩纹理强制线性布局、test32 preload 修复、test36 r62 帧参数修复、256 工作组上限、能力位及诊断默认关闭均保留。
- 重新检查 test30 的实际复制 FS NIR（`tools/work/eden-test38-afbc/old-upload-fragment-nir.txt`）：读取 `load_frame_arg_pan`，提取低八位作图层并参与源 buffer 地址计算。test36 已修复 G2 r62 帧参数预载；这解释了旧 AFBC 上传故障也可能受同一缺陷影响。新候选实际 AFBC 复制全部通过，未通过移除图层索引规避问题。
- 通过 `VK_EXT_image_compression_control` 查询实际图像压缩属性并作对照：test37 flags=0x4、159744 字节；test38 flags=0、405504 字节；test38 显式禁用压缩后又为 flags=0x4、159744 字节。PanVK 的查询实现只在 AFBC 图像返回 DEFAULT=0。测试控制不依赖打开驱动诊断。
- 回归通过：原 test30 上传六例（四个 RGBA/BGRA 实际 AFBC、两个不支持 AFBC 的 16 位格式），额外 18 例 mip/array/image copy/split upload/透明 LOAD/四种深度格式/BC 与 ASTC；当前 MSAA 24/24、原厂 24/24；Android 图形 9/9、Eden 计算 9/9；诊断审计四模式通过，默认/显式关闭均零 UDP socket、信号 handler 变化、datagram 和文件字节。交付 stripped SO 的白底透明 LOAD+深度+索引组合复核通过。
- R11 float 混合的旧 8 位 CPU oracle 在原厂、test37、test38 都同样失败。未放宽容差；四帧完整原始输出在三者间 SHA-256 完全一致。独立报告 `tools/fixtures/test38-r11-blend-reference.json`；初始失败记录仍保留在 `tools/work/eden-test38-afbc/extended-initial.json`。18 例扩展回归另含 R11 不混合 LOAD 用例。
- 测试包：`/storage/emulated/0/Download/panvk-mali-g2-test38-perf-bfcc5d7-6f810679.zip`；ZIP SHA-256 `927cd6585198b6e9abd735f04b3c637ca0a8dd1ec03e9299706ab8c7011ffa8c`；库 SHA-256 `6f8106792266821957124801c8f69c7d357e864922bb99316273c9ebd4043a90`。完整来源、唯一增量、清单在 `tools/artifacts/test38/`，符号 `tools/artifacts/symbols/test38-unstripped.so.zst`。
- 用户确认 test38 同一室内约 45 FPS，实际加载库哈希也已验证，未达到 60 FPS；后续采样和 test39 进展见上方。不要擅自恢复异步 GPU 或用完整重诊断掩盖问题。

## test37 闪退定位记录：已通过 Android root 取得本轮日志

- 用户指出 Android 已 root，历史 Opus 可直接取日志；已核实历史成功记录。PRoot 中 `/system/bin/su` 的 ENOENT 不能用来否认 root。通过 Play 商店版 Termux 原生命令服务调用 `su` 成功，返回真实 `uid=0(root) context=u:r:ksu:s0`。入口与调用方法见 `BUILD_AND_TEST.md` 的 Android root 日志采集段。
- 2026-10-29 23:12 UTC 取得当前 `eden_log.txt`、`.old.txt`、210 MiB `eden_gpu.log`、crash logcat 与 dmesg，保存在 `tools/work/eden-test38/eden-logs/`。补充配置、应用 logcat 和四份系统 tombstone 在 `tools/work/eden-test38/eden-details/`。日志中的设备时区为 UTC+8，最新崩溃是 10 月 30 日 07:04。配置选中 test37-perf；未能读取私有安装库做 SHA 对照，不能声称已校验实际加载库哈希。
- 本轮三次 SIGSEGV：06:58:16 GPU 线程 `PipelineCache::CurrentGraphicsPipelineSlowPath()+744`；07:02:28 Thread-16 `GraphicsPipeline::Validate()+164`；07:04:21 nvservices 线程 `Scheduler::BeginRenderPassImpl()+264`，来自 `RealizeDeferredClear -> FlushDeferredClear -> RemoveFramebuffers -> DeleteImage -> UnmapMemory`。均在 `libyuzu-android.so`，不能仅据此排除驱动影响。
- 已反汇编实际安装的 Eden 库核对指令。第二次崩溃 `this=0xb40000762f503d30`，tombstone 的 memory near x0 开头是 `0x07230203` SPIR-V magic，后面是完整着色器头和指令；Validate 把这些数据当作 descriptor vector 读，触发 `0x0003004700000046` 非法地址。强烈指向对象失效/内存复用；尚未证明释放来源。第一次在 CreateGraphicsPipeline 返回后访问原 map bucket 失败；第三次读 Scheduler 的空 command chunk 指针失败，不能简单称为 framebuffer 空指针。
- 三次 SIGSEGV 的 Abort message 留有早先 vkCreateDevice 缺少扩展依赖的文字。它不是三次当前 SIGSEGV 的真实调用栈，不要误判为此处 SIGABRT。当前 dmesg 未发现与这些崩溃对应的 Mali CS/page fault 记录；不等于排除驱动错误。
- Eden 配置显示 `use_asynchronous_gpu_emulation=true`、`use_asynchronous_shaders=false`、`debug=true`（Vulkan validation）、`gpu_log_level=2`、shader dumps=true。驱动诊断总开关属性为空，旧 `debug.mesa.panvk.debug=sync` 仍存在但 test37 默认忽略。Eden 自己的日志和 validation 仍开着，可能影响性能；本轮未修改它们。
- 后续用户确认关闭异步 GPU 后不再闪退；性能跟进、经确认的应用诊断设置修改及 test38 候选见本文件顶部。异步根因仍未修复。
- 取日志阶段没有修改驱动实现或恢复逐提交等待；后续 test38 仅恢复颜色 AFBC，未修改同步实现。test36 是历史成功基线，test37 加关闭异步 GPU 是用户新确认的稳定对照，test38 游戏验证仍待完成。

## test37 性能版记录（用户报告游戏闪退）

- 用户在确认 test36 成功后明确要求：**把驱动诊断都关掉，性能差，出问题再开**。后续遵守这一偏好，不要自动为正常游戏启用诊断或启动常驻监视器。
- 新总开关 `PANVK_G2_DIAGNOSTICS` 默认 false；关闭时跳过 PANVK_DEBUG 和调试文本配置、CPU tracing 初始化。Android 残留的 `debug.mesa.panvk.debug=sync` 仍存在，但性能版默认忽略它，不再强制逐提交等待 GPU。
- 默认关闭 GPU breadcrumb、绘制/dispatch/fragment 工作记录、描述符转储、NIR 序列化和 UDP/文件日志、信号崩溃处理器、BO 地址登记/扫描与释放历史记录。正常的依赖同步、队列故障处理和渲染实现保留。
- 重新排错必须显式设置 `PANVK_G2_DIAGNOSTICS=1`（Android 对应 `debug.mesa.panvk.g2.diagnostics=1`），再配合 `PANVK_DEBUG=sync`。选项进程内只读取一次，切换后重启应用。保持诊断关闭时设为 0 或不设置总开关。不要在没有新的排错请求时执行这些开启操作。
- 集成审计 `tools/test_diagnostics.py` 用原生测试程序采集前后 UDP socket 与信号 handler 状态，接收 UDP 并检查文件字节数：test36 基线和显式开启各 1 个 socket、5 个 handler、56 个数据包、4261 字节日志；默认关闭和显式关闭均全部为 0，四种模式实际绘制全部通过。报告：`tools/fixtures/test37-diagnostics.json`。审计接收器已关闭，没有常驻 monitor。
- 关闭诊断、采用正常异步路径时，MSAA 当前驱动 24/24、原厂对照 24/24，图形 9/9、计算 9/9 通过。报告为 `test37-msaa.json`、`test37-android.json`、`test37-eden.json`。文字白底 LOAD/透明混合回归在诊断审计四个模式内也通过。尚未测得用户游戏 FPS，不能声称具体性能提升倍数。
- 性能包：`/storage/emulated/0/Download/panvk-mali-g2-test37-perf-bfcc5d7-bfc90b6f.zip`；ZIP SHA-256 `697b7046659ad51b3a1e507cd486631d431e98debd0c2f5a46f8cc1fd6707d58`。源码和清单 `tools/artifacts/test37/source.patch`、`build.json`；符号 `tools/artifacts/symbols/test37-unstripped.so.zst`。与 test36 的三个关键渲染补丁逐项比较一致。安装切换后彻底重启模拟器，以清除旧驱动的进程状态。
- 保留 test36 已确认的渲染修复及 AFBC/压缩纹理布局回退，不开启任何新功能位。test36 用户确认记录及其测试包继续保留，便于回退。
- 上轮 Eden 日志还显示 `Renderer.debug: true` 和模拟器自己的 GPU logging。它们属于应用侧，独立于驱动诊断总开关；如仍开启，需要在 Eden 设置里关闭图形调试/验证层。没有修改应用私有配置。

## test36 记录（已由用户确认成功）

- 在交付 test36 并请求验证博士开场和瞬间花屏后，用户回复 **“成功了!”**。以 test36 为当前可用基线，保留已有文字、图像布局及片元帧参数修复。用户未提供逐项细节或长时间稳定性数据，不扩展为全功能或长期稳定性保证。
- 收到用户确认时诊断监视器已不在运行，已有监视日志归档在 `tools/work/eden-test36/driver-final.log`。驱动包及其哈希、源码补丁、符号和回归报告均保留；没有继续修改渲染代码或运行时调试属性。
- GS/geometryStreams 等未实现能力仍按真实支持情况报告。本次成功不代表新增了这些功能；若未来其他游戏触发相关错误，应作为单独问题处理。

## test36 修复与验证记录：G2 片元帧参数预载

- test35 已成功采集应用日志。保存于 `tools/work/eden-test35/driver-game-first.log`，筛选文本 `eden-forwarded.txt`，按 Eden 时间排序文本 `eden-sorted.txt`。同一应用 PID 23946 在多次切驱动期间没有重启，文件包含早期会话；本轮 test35 创建设备对应 Eden 时间约 788 秒，分析本轮时使用 788 秒以后的记录。
- 本轮两次明确管线异常：791.111773、801.334896 秒的 `Geometry streams is not implemented`。另有 1024 线程计算 shader 超过报告上限 256、带 STORAGE 用途的 R11G11B10 4x 图像不受支持、layout/threading/query 等验证层报错。NVDEC 设置为 Cpu；不要把 67 秒的旧会话视频启动事件误认为本轮开场。末尾还有 PresentThread SIGABRT，触发背景未知，不能等同于 GPU fault。
- **独立复现有效 API 下的 MSAA 解析全黑错误**：source 仅用 COLOR_ATTACHMENT/SAMPLED/TRANSFER 用途、先 ClearColorImage，再通过计算 texelFetch 验证每个样本，最后 CmdResolveImage 回读。原厂正常，test35 的每个样本正确，但解析结果全黑。RGBA8/RGBA16F/R11G11B10 及数组图层均能复现。
- **根因与修正**：`compiler/bifrost/valhall/va_pack.c` 的 G2 fragment preload 只允许旧 r58-r61，漏掉帧参数低位 r62。普通内部 resolve shader 通过帧参数读 gl_Layer，未预载时拿到错误图层编号并从范围外采样。test36 将旧 r62 映射到 G2 硬件输入 r4，并把允许掩码扩大为 r58-r62。**没有猜测或添加 r63 高位映射**。
- test36：2/4/8/16 倍采样 × RGBA8/RGBA16F/R11G11B10 × layer0/layer2，共 24/24 通过；原厂 24/24 通过；归档 test35 的 6 个目标场景均失败，形成 A/B。数组用例将所有三层一起 resolve，只给第 2 层非零颜色，能排除简单返回固定层 0 的错误实现。
- 数组测试早期遗漏两次重叠 ClearColorImage 之间的 WAW 屏障，原厂出现少量像素差异；已补 TRANSFER_WRITE→TRANSFER_WRITE 屏障，原厂全部通过。不要把这个用例同步错误记录成原厂驱动缺陷。早期结果保存在 `tools/work/eden-test36/msaa-before-test-sync-fix.json`。
- 验证：`tools/fixtures/test36-msaa.json`，Android 9/9、Eden 计算 9/9，白底透明 LOAD 局部绘制文字回归通过。host `panfrost_compile` 已重编并更新 `build-host/bin`，Android 预编译 shader 和最终驱动已重建。
- 新工具 `tools/vulkan_msaa.c`、`tools/test_msaa.py`、`fixtures/msaa_sample.comp`。`msaa_store/load.comp` 用于初步 storage 探测；当前 shaderStorageImageMultisample 仍为 false，不能将强制 probe 的结果当作受支持 API 能力。test36 未改任何功能位或 256 工作组限制，也保留 AFBC、压缩纹理与文字 LOAD 回退/修正。
- 测试包：`/storage/emulated/0/Download/panvk-mali-g2-test36-bfcc5d7-b3bee661.zip`；ZIP SHA-256 `b6c2e84bc9e1e7769f0f9cca8ec5b5e3a4c050b437c43cf89f9326b9e03f6b64`。完整源码补丁与清单在 `tools/artifacts/test36/`，符号 `tools/artifacts/symbols/test36-unstripped.so.zst`。本地未提交、未推送。监视器 `/storage/emulated/0/Download/panvk-g2-test36-driver.log`，PID 文件 `tools/work/eden-test36/monitor.pid`。
- **用户已确认 test36 成功**。此版修复了已复现的解析黑屏；GS 管线异常对应的功能仍未实现，不应将其与本次成功混同。

## test35 记录（已成功取得游戏日志）

- 用户反馈 test34 仍是相同开场错误，画面未修复。游戏 PID 23946 共收到 49,557 条驱动事件、2 次 device begin，已有 2913 次 compile end result=0，没有 GPU fault 或崩溃事件。快照 `tools/work/eden-test34/driver-game-first.log`，提取出的日志事件 `eden-forwarded.txt`。
- test34 成功发现实际打开的日志路径，但 `fopen` 同一路径仍 ENOENT。**采集失效的关键遗漏是 adrenotools 的文件重定向**：Eden 在 renderer_debug 时启用 `ADRENOTOOLS_DRIVER_FILE_REDIRECT`，其命名空间的 `fopen` 会为非 `/proc`/`/sys` 文件加上重定向目录。此时路径真实存在也会报 ENOENT。来源： https://github.com/bylaws/libadrenotools/blob/master/src/hook/hook_impl.cpp 、 https://raw.githubusercontent.com/bylaws/libadrenotools/master/src/hook/file_redirect_hook.c 、 https://github.com/eden-emulator/mirror/blob/master/src/android/app/src/main/jni/native.cpp 。
- test35 对诊断日志改用 `syscall(SYS_openat, ..., O_RDONLY | O_CLOEXEC)` 后 `fdopen`，避免已知的 `fopen` 重定向。当前会话日志新 inode 从开头分段读取，旧会话仍取末尾 256KiB；每次读取最多 256KiB，轮询间隔 5 秒。
- 新增 `tools/log_redirect_hook.c` 及 `test_log_capture.py --fopen-hook`，在原生 GPU 进程模拟日志 fopen 的 ENOENT。**A/B 已复现并修正采集错误**：归档 test34 库在两种日志名均绘制正常但没有采集到内容；test35 两种日志名均成功采集 renderer/video/Critical，组合深度+索引+LOAD 绘制也正常。报告：`tools/fixtures/test35-log-hook-baseline.json`（预期失败）、`test35-log-hook.json`（通过）。基础图形 9/9 通过：`test35-diag-android.json`。
- ZIP：`/storage/emulated/0/Download/panvk-mali-g2-test35-diag-bfcc5d7-99b85ecd.zip`，SHA-256 `f9bca7a67df00102a7d9b949043cafb08c8f0461cefca4a1744c43be0bc488ca`，源码补丁和清单在 `tools/artifacts/test35/`，符号 `tools/artifacts/symbols/test35-unstripped.so.zst`。当前监视器输出 `/storage/emulated/0/Download/panvk-g2-test35-driver.log`，PID 文件 `tools/work/eden-test35/monitor.pid`。
- 这一改动修复诊断，不代表动画已修复。test35 不改图形管线或功能位。后续应先读取本轮真正的 Eden 错误，避免仅凭旧的 GS 日志或“动画”一词猜测解码或几何阶段。

## test34 记录（未修复开场，日志仍被重定向）

- **最新用户实测覆盖旧结论**：test33 已能渲染 3D，后续正常游戏画面正常；剩余问题是开场博士介绍动画没有显示，以及瞬间花屏。不要继续把“全部 3D 不渲染”当作当前状态，也不要将几何着色器缺失直接等同于剩余问题。
- test33 与 test32 的图形修复相同，仅增加日志采集。因此不能声称 test33 实现了 GS 或新增 3D 渲染能力；正常游戏画面是用户当前实测结果。
- 收到游戏进程 PID 17650 的 63,864 条驱动事件，着色器 ready：VS 2860、FS 2860、CS 27；没有编译错误或 GPU fault/DEVICE_LOST 事件。约 212811 单调秒时记录到 PresentManager/PresentThread 的 SIGABRT；它是否与退出有关尚未知，不能据此断言是开场花屏原因或正常退出。
- 游戏事件快照：`tools/work/eden-test33/driver-game-first.log`；纯文本 `game-events.txt`；NIR 在 `nir/shader-17650-*`。**test33 的游戏日志采集失败**：游戏进程内部读取硬编码路径也报 ENOENT，因此没有取得本轮 Eden renderer 错误。这不能作为“模拟器没有错误”的证据。
- test34 改用 `/proc/self/fd` 发现应用实际打开的 `eden_log.txt` 或 `yuzu_log.txt`，再按解析出的真实路径只读打开；同时捕获视频/NVDEC、renderer 设置及 Critical 记录。通过继承 write-only 日志 FD 的原生 GPU 集成测试验证两种名称，渲染/视频/Critical 内容均收到，其他 Info 被过滤；组合深度、索引和背景 LOAD 绘制同时通过。报告：`tools/fixtures/test34-log-fd.json`。Android 基础图形 9/9 通过，报告 `test34-diag-android.json`。
- PRoot 下直接重开 `/proc/self/fd/N` 会导致读取异常，故通过 fd 查找真实路径后重新打开真实文件，而非读取 proc fd。失败对照保存在 `tools/work/eden-test34/log-fd-before-fix.json`。
- test34 ZIP：`/storage/emulated/0/Download/panvk-mali-g2-test34-diag-bfcc5d7-fd539aea.zip`，SHA-256 `1e887363f117c8de3cfd8e84ff36da3b26f9a4a064b72af7f9aa166295d8f550`；源码补丁、清单与符号已归档。监视器输出 `/storage/emulated/0/Download/panvk-g2-test34-driver.log`，PID 文件 `tools/work/eden-test34/monitor.pid`。保留 test33 ZIP 作为正常游戏基线。
- 当前待确认开场是整个动画区域缺画面还是只有模型缺失；已用异步问题请求用户区分。尚未改变渲染能力位、纹理布局或解码策略。

## test33 记录（用户已确认后续游戏 3D 正常）

- 用户明确确认：**test32 文字正常**；test33 后续游戏 3D 也正常，剩余开场问题见上方。test32 的修复已得到游戏实测支持。
- test33 是保留 test32 修复的诊断包，新增 `panvk_g2_trace_eden_log()`：在 G2 kbase 的 sync 提交路径里，最多每 5 秒读取 Eden 自己的当前/旧日志，筛选 renderer、shader、错误和警告，经现有本机 UDP 监视器转发。Termux 进程不能直接读取该应用的私有外置目录；代码在 Eden 进程内运行。应用实测发现硬编码日志路径 ENOENT，后续 test34 改为自动发现。
- ZIP：`/storage/emulated/0/Download/panvk-mali-g2-test33-diag-bfcc5d7-911a8f33.zip`，SHA-256 `512ea239bb5d6a9cc654a71fa681af3e04ba90091c14a3109c9ef04286c5b11c`。这是诊断版本，**没有宣称修复 3D**。源码与符号：`tools/artifacts/test33/source.patch`、`build.json`、`tools/artifacts/symbols/test33-unstripped.so.zst`。
- 已通过 Android 9/9，以及白底、局部 renderArea、混合、LOAD 内实际绘制的文字修复回归。新增深度绘制用例：D16/D32/D24S8/D32S8，CLEAR 绘制与保存 CLEAR 后 LOAD 绘制，正常/反向深度比较，片元通过/拒绝，原厂 32/32、test33 32/32 通过。报告：`tools/fixtures/test33-depth.json`。
- 补充索引绘制：16/32 位索引、firstIndex=7、vertexOffset 为 0/-1/-30000，结合 D24S8 和局部 LOAD 绘制，原厂和 test33 各 6/6 通过。报告：`tools/fixtures/test33-indexed.json`。这些结果只覆盖基础路径，不能证明真实游戏 3D 已恢复。
- 本机能力查询保存在 `tools/work/eden-test33/caps-{vendor,panvk}.txt`：原厂 geometryShader=1、geometryStreams=1，PanVK 两项为 0。原厂 shaderOutputViewportIndex=1、shaderOutputLayer=1；PanVK 为 0、1。**不要误写原厂不支持 geometryStreams**；用户已告知同一游戏原厂也无 3D，所以不能只凭 PanVK 缺 GS 就断言全部原因。
- Eden 源码 `renderer_vulkan/vk_pipeline_cache.cpp` 根据 `VK_EXT_shader_viewport_index_layer` 是否暴露决定是否生成 layer passthrough GS；其 SPIR-V backend 在没有 geometryStreams 时连 stream 0 都抛出异常。历史 test28 日志确有 `Geometry streams is not implemented`，但仍需当前日志确认。Vulkan 扩展说明要求 multiViewport，不能只改 extension 字符串来绕过（https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_shader_viewport_index_layer.html）。没有改动这些能力位。
- 监视器输出：`/storage/emulated/0/Download/panvk-g2-test33-driver.log`；PID 文件 `tools/work/eden-test33/monitor.pid`。已向用户请求安装 test33 并进入缺 3D 场景运行约 30 秒。test32 最终日志快照在 `tools/work/eden-test32/driver-final.log`。现已收到游戏驱动事件，但本轮 Eden 日志采集失败，详见上方。

## test32 记录（用户已确认文字正常）

- 用户确认 test31 黑块有所改善但仍在，描述为文字或动态区域的黑色覆盖。已查看 39.381 秒 HEVC 录屏 `/storage/emulated/0/DCIM/ScreenRecorder/Screenrecorder-2026-10-29-20-26-39-979.mp4`（3408×2272）：宝可梦对话框边框和白底正常，文字行附近出现随动画变化的黑色矩形。关键帧在 `tools/work/eden-test32/video/`。
- **复现剩余覆盖的一个确定驱动错误**：先用 CLEAR 建立白色背景，再在 LOAD 通道里实际绘制透明纹理，test31 丢失原背景、出现黑色或错误颜色；重新绑定管线、禁用 CRC、全局 linear 均不能修复。原厂在同样的全区域及非对齐局部 renderArea 上正确。
- **根因**：`panvk_vX_cmd_frame_shaders.c:get_frame_shader` 创建内部 framebuffer load/resolve 着色器的 SPD 时仍使用 `shader->info.preload >> 48`。G2 编译器已把寄存器预载掩码重映射到低位，此移位使其归零；普通 shader 的 SPD 路径早已按 G2 使用低位，但内部 frame shader 路径遗漏。test32 改成与普通 shader 一致的架构条件移位。
- **修正历史排除项**：以前的空 LOAD/STORE 测试没有实际绘制，可能被驱动直接省略，不能证明背景预载正确。必须在 LOAD 通道中执行绘制，并验证透明像素和裁剪区域外的非零背景。不要再把“LOAD 保留内容”列为已排除。
- 修正后 RGBA8/BGRA8/RGB10A2/RGBA16_UNORM/RGBA16_FLOAT/RGBA32_FLOAT 各测完整与局部 renderArea，共 12/12 通过；另有 BC1/BC3/BC7/ASTC + 非零 mip/layer + 分区域上传 + image→image copy + LOAD 透明绘制的 4/4 通过。图形回归 9/9、Eden 计算回归 9/9 通过。
- R11G11B10_FLOAT 在白底局部 LOAD/混合测试的 8 帧共 1,060,896 字节与原厂逐字节一致。该格式的近似 CPU 混合模型对两驱动都报相同舍入差异，因此用原厂原始输出作严格对照，不能将该 CPU 模型失败误报成 PanVK 独有错误。
- 完整报告：`tools/fixtures/test32-framebuffer-load.json`、`test32-android.json`、`test32-eden.json`。普通混合、双源混合、discard、隐式 UV 采样、UBO + 非零 descriptor binding、宽格式等控制测试保存在 `tools/work/eden-test32/`；这些控制测试没有复现独有错误。
- 测试包：`/storage/emulated/0/Download/panvk-mali-g2-test32-bfcc5d7-08eb2bb1.zip`；ZIP SHA-256：`309c9271e42a8491c733bdb10843af87fac90bf9a5390565aeab34e0007cfd9b`。它是 `bfcc5d7` 加未提交补丁的本地构建，完整 driver patch 与哈希在 `tools/artifacts/test32/source.patch`、`build.json`，符号版在 `tools/artifacts/symbols/test32-unstripped.so.zst`。
- 保留 test30 的禁用 AFBC、256 线程上限和 test31 的压缩纹理线性布局。用户已确认 test32 文字正常；3D 问题尚未解决，没有修改能力位。
- 视频分析使用本地 `tools/build/media-deps` 中的 PyAV/Pillow；安装 ffmpeg 的 pacman 下载遇到镜像 403，事务未提交，未升级系统包。原 pacman 配置未修改，临时配置及日志在 `tools/work/eden-test32/ffmpeg-*`。

## test31 记录（用户确认黑块改善但未消失）

- 用户已确认 test30 能持续进游戏、不再闪退；黑块和无 3D 仍在，并再次确认原厂在同一 Eden 里也不渲染 3D。test30 本轮驱动日志无 fault/crash 标记，快照在 `tools/work/eden-test30/driver.log`。
- 扩展 `vulkan_tiles`：加入逐像素验证的实际图形绘制、透明混合、移动裁剪、非纯色 LOAD/STORE 保留、R8 文字遮罩 swizzle、RGB565/ARGB1555 等测试。原厂和 test30 的图形对照 8/8、LOAD 对照 8/8 均通过；不能据此排除游戏中特定用法，但普通绘制与文字格式的基础路径没有复现错误。
- **独立复现压缩纹理损坏**：同一组有效 BC1、BC3、BC7 压缩块在原厂正确，test30 在上传/回读后采样和实际绘制均产生大片零值/黑色像素。ASTC 常色块采样及绘制正确，但原始压缩块回读不一致。原厂四种格式的上传、原始块回读、采样和绘制均通过。
- test30 加 `PANVK_DEBUG=sync,linear` 后四种格式全部正确，隔离到压缩纹理平铺寻址/重解释路径。仅修正 generic plane 描述符的重解释尺寸没有改善，已撤回该尝试，`pan_texture.c` 没有保留改动。
- test31 仅在 G2、逻辑 `VK_IMAGE_TILING_OPTIMAL` 且格式压缩时强制选择线性 modifier；显式 modifier 导入和普通纹理选择不变。保留 test30 禁用 AFBC、256 线程上限的配置。这是经过 GPU 验证的布局回退，尚未修复压缩格式的平铺寻址本身。
- test31 的 BC1/BC3/BC7/ASTC 四种格式各测 257×129、1280×720、mip 2 + array layer 2，共 12/12 通过，后两类还含混合、裁剪及 LOAD/STORE 保留；原厂新增 mip/array 对照 4/4 通过。此前原始块、采样及绘制的错误均消失。图形和 Eden 计算回归各 9/9 通过，总计 30 项新驱动场景通过。完整报告：`tools/fixtures/test31-textures.json`、`test31-android.json`、`test31-eden.json`，逐例日志在 `tools/work/eden-test31/`。
- 测试包：`/storage/emulated/0/Download/panvk-mali-g2-test31-bfcc5d7-8a034076.zip`；ZIP SHA-256：`fbbeafef8306b23b0a1def4f2f4cd055194842d879a016150db0bf5f8ebb157e`。它是 `bfcc5d7` 加未提交补丁的本地构建，源码与哈希在 `tools/artifacts/test31/source.patch`、`build.json`，符号版在 `tools/artifacts/symbols/test31-unstripped.so.zst`。
- Eden 黑块是否与本次复现同源，待用户安装 test31 验证。3D 缺失尚未解决：历史 Eden 日志 `tools/work/eden-test28/eden_logs/eden_log.txt` 有 `Geometry streams is not implemented`，说明部分 graphics pipeline 在模拟器侧创建失败，但这些旧日志不足以证明当前所有 3D 缺失的原因。没有修改或虚报 GS、tessellation、geometryStreams 等功能位。

## test30 记录（用户已确认不再闪退）

- 用户实测 test29 仍闪退、有黑块，反馈“不如 test27”。test29 日志已保存为 `tools/work/eden-test29/driver.log`；确认 Eden 进程 pid 25721 加载的是 256 线程上限。serial 1651 的 fragment work 25 已完成、26 已开始；work 26 又是 2560×1440、RGBA8、AFBC 的通道，与 test28 一致，单独回退工作组上限无效。
- test30 在 G2 上默认禁止为驱动分配的图像选择 AFBC，覆盖颜色与深度/模板图像，工作组上限保留 256。目的是恢复接近 test27 加 `noafbc` 的稳定配置；这是禁用故障路径的临时方案，尚未修复 AFBC 本身，黑块仍未解决。
- 新增 `tools/vulkan_tiles.c` 与 `fixtures/tiles_sample.comp`，每帧更新非纯色图案，分别逐像素验证 buffer→image→buffer 与纹理采样→SSBO。8 帧测试覆盖 RGBA8 的 257×129、1280×720、2560×1440，BGRA8 的 257×129，以及 R5G5B5A1 的 257×129、1280×720。
- **已独立复现 GPU 故障**：原厂 6/6 通过；test29 的 4 个 RGBA/BGRA 场景报 DEVICE_LOST，两个 R5G5B5A1 场景通过；test30 的 6 个场景全部通过。再用 test29 加 `PANVK_DEBUG=sync,noafbc` 重跑 257×129 RGBA8 也通过，形成 AFBC 开关对照。报告：`tools/fixtures/test30-tiles.json`，逐例日志：`tools/work/eden-test30/`。
- 独立复现的 crumbs 显示第一个 fragment 工作就失败，后续 image→buffer 与计算采样都尚未开始；对应 buffer→AFBC 图像上传阶段。故障日志和 NIR 在 `tools/work/eden-test30/repro-driver.log`、`repro-nir/`。不要将这项已复现的闪退问题等同于黑块原因。
- Android 图形与 Eden 计算回归各 9/9 通过，报告为 `tools/fixtures/test30-android.json`、`test30-eden.json`。
- 测试包：`/storage/emulated/0/Download/panvk-mali-g2-test30-bfcc5d7-1fb884d4.zip`；ZIP SHA-256：`51cd0a1f0e97994fa9b28d7e9549f82157f4984bc3202c26339a6db27ef1b31e`。这是基线 `bfcc5d7` 加未提交补丁的本地构建，源码补丁与哈希在 `tools/artifacts/test30/source.patch`、`build.json`，符号版为 `tools/artifacts/symbols/test30-unstripped.so.zst`。
- 用户已确认恢复稳定进游戏；后续黑块调查见上方 test31 记录。Android 调试属性仍为 `sync`，无需用户另外设置 noafbc。

## test29 记录（用户已确认仍闪退）

- test29 仅删除 `kbase_kmod.c` 中将工作组上限强制提升至 1024 的覆盖，恢复本机内核报告的 256；保留 test28 的 ZS 无 AFBC 改动。
- 本机能力查询确认 `maxComputeWorkGroupInvocations=256`、三个 `maxComputeWorkGroupSize` 均为 256。Android 图形测试 9/9、Eden 计算套件 9/9 通过；剥离后的交付库纹理绘制 5/5 帧、256/256 像素通过。报告在 `tools/fixtures/test29-android.json` 和 `test29-eden.json`，构建与能力日志在 `tools/work/eden-test29/`。
- 诊断包：`/storage/emulated/0/Download/panvk-mali-g2-test29-bfcc5d7-e16e7252.zip`；ZIP SHA-256：`18a6aaf2ddf559b3ef8f637b22f1fa277a4172c1e9cf4cfe89350991d6b82771`。它是基线 `bfcc5d7` 加未提交补丁的本地构建；可复现补丁与哈希在 `tools/artifacts/test29/source.patch`、`build.json`，符号版在 `tools/artifacts/symbols/test29-unstripped.so.zst`。尚未提交或推送。
- 用户实测仍闪退并有黑块，不能视为已修复版；后续进展见上方 test30 记录。
- **修正第 5 节的一项推测**：重放 test28 捕获的 `shader-27522-2855-5-1.bin` 得到 `internal: true`、`workgroup_size: 64, 1, 1`，因此 shader 2855 的出现不能证明 Eden 采用了 1024 线程路径。NIR 保存在 `tools/work/eden-test29/shader2855.nir.txt`。
- test28 故障 crumbs 为 serial 1861，fragment work 32 已完成、33 已开始。work 33 是 2560×1440、颜色附件 AFBC 的 pass，计算 work 29 已完成；这仍需排查颜色 AFBC 路径，不能直接归因于大工作组。
- Git 已修复：104 个不可读取的 PRoot 链接对象恢复为普通文件，完整性与新对象读写验证通过；原对象目录保留在 `.git/objects-proot-backup-20261029`。

## 1. 工作环境

| 项 | 值 |
| --- | --- |
| 设备 | Xiaomi M367FC（yingtian），SoC 玄戒 XRing O3 |
| GPU | Mali-G2-Ultra-NX MC16，Valhall v15（PAN_ARCH 15），`/dev/mali0` kbase 1.39 CSF |
| 原厂驱动 | `/vendor/lib64/hw/vulkan.mali.so`，Vulkan 1.4.345，可作对照 |
| 系统 | Android；Termux 内跑 proot Manjaro ARM64（`/root` 在 proot 里） |
| 权限 | Termux 有 root：proot 内 `su -c '...'` 可用；Android 系统命令需经 `/system/bin/sh -c "su -c '/system/bin/<cmd> ...'"`，例如 `setprop`/`getprop` |
| 包管理 | `pacman` / `yay` 可装依赖 |
| 仓库 | `/root/mali-g2-project`（单仓库，`main`），远端 https://github.com/freefrank/mali-g2-panvk （public） |
| 目标 | 只适配玄戒 O3 / Mali-G2 v15，不考虑其他设备兼容性 |
| Eden | 包名 `com.miHoYo.Yuanshen.nightly`（Eden 改名版），日志目录见第 4 节 |

约定（`driver/AGENTS.md`）：提交信息必须带 `Generated-by: LLM`，文档带 `biblioklept` 标记；禁止在 Mesa GitLab 发帖/评论/开 MR。上游 AGENTS.md 对 `src/panfrost` 的 AI 禁令已移除，本 fork 可改。

git 若报 dubious ownership：已设 `safe.directory '*'`；proot 中用 sudo 后需 `chown -R 10312` 回 Termux UID。

## 2. 构建与发布

```sh
# Android 驱动（增量）
env LC_ALL=C PATH=/root/mali-g2-project/driver/build-host/bin:/root/mali-g2-project/toolchain/bin:/usr/bin:/bin:/usr/sbin:/sbin \
  ninja -C /root/mali-g2-project/driver/build-android-arm -j3 src/panfrost/vulkan/libvulkan_panfrost.so

# 发布 testN（先 git commit，再构建，否则 Mesa SHA 不对）
cd /root/mali-g2-project
SO=driver/build-android-arm/src/panfrost/vulkan/libvulkan_panfrost.so
mkdir -p tools/artifacts/testN
llvm-strip -o /tmp/tN.so $SO
zstd -q -f $SO -o tools/artifacts/symbols/testN-unstripped.so.zst   # 符号版，用于还原崩溃地址
python3 driver/android/package.py /tmp/tN.so tools/artifacts/testN testN
rm -f /storage/emulated/0/Download/panvk-mali-g2-test*.zip            # Download 只留当前包
cp tools/artifacts/testN/*.zip /storage/emulated/0/Download/
git push -q origin main
```

包名含标签、提交和库哈希，保证 Eden 会替换驱动（同名包 Eden 不会重新加载）。
注意：修改 `pan_shader_info` 等结构后需重建 host 的 `panfrost_compile` 并删除 `libpan_shaders_v*`，否则预编译 shader 过期。

## 3. 本地 GPU 测试（无需 Eden）

统一前缀：`E="env -u LD_PRELOAD LD_LIBRARY_PATH=/vendor/lib64:/vendor/lib64/egl:/system/lib64"`，
`NEW=/root/mali-g2-project/driver/build-android-arm/src/panfrost/vulkan/libvulkan_panfrost.so`，在 `tools/` 下运行。

| 命令 | 用途 |
| --- | --- |
| `$E MALI_G2_TEST_FRAMES=5 build/bin/vulkan_draw $NEW build/shaders/texture.vert.spv build/shaders/texture.frag.spv texture` | 基础绘制+纹理；环境变量见 `vulkan_draw.c`（`MALI_G2_TEST_VERTS/INSTANCES/NO_DRAW/SWIZZLE/TEX_FORMAT/ALPHA_BLEND/LOGIC_OP/VERTEX_BINDINGS`） |
| `python3 test_eden_compute.py $NEW --tag tX` | Eden 计算着色器套件（应 9 PASS） |
| `build/crashprobe/oob $NEW build/crashprobe/oob.spv <stride> <robust>` | SSBO 越界钳制 |
| `build/crashprobe/preload $NEW <VkFormat> 1280 720 <iters> [clear_in_pass] [force_fault]` | 空 LOAD/STORE 渲染通道保留内容；`force_fault=1` 人为制造 GPU fault，用于验证 fault 报告 |
| `build/crashprobe/wg $NEW build/crashprobe/wg1024.spv 1 0` | 1024 线程工作组 + shared + barrier |
| `build/devcaps/devcaps <driver.so>` | 打印全部 feature/limit/扩展；原厂与 PanVK 对比结果在 `work/eden-test28/caps-*.txt` |
| `build/bin/replay_nir <shader-*.bin> out.o`（`MALI_G2_REPLAY_PRINT_NIR=1` 打印 NIR 到 stderr） | 重放 monitor 捕获的着色器 |

crashprobe 源码在 `tools/build/crashprobe/*.c`，编译：
`env -u LD_LIBRARY_PATH -u LD_PRELOAD /data/data/com.termux/files/usr/bin/clang -no-canonical-prefixes -std=c11 -O2 -I$R/references/malibridge/third_party/Vulkan-Headers-1.4.341/include -I$R/driver/include -I$R/driver/include/android_stub x.c -o x -ldl`（`R=/root/mali-g2-project`）。

## 4. 读取日志

### 4.1 驱动调试开关

驱动读取 Android 属性 `debug.mesa.panvk.debug`（等同 `PANVK_DEBUG`），Eden 无法传环境变量，只能用属性：

```sh
timeout 30 /system/bin/sh -c "su -c '/system/bin/setprop debug.mesa.panvk.debug sync; /system/bin/getprop debug.mesa.panvk.debug'"
```

- `sync`：每次 submit 后等待完成；开启 G2 fault 诊断（crumbs、work 日志、描述符回读）。当前保持 `sync`。
- `noafbc`：全局禁用 AFBC（可与 sync 组合：`sync,noafbc`）。
- `trace`：会让 Eden 崩溃，不可用。
- 备用：`/storage/emulated/0/Download/panvk-debug.txt` 文件内容也会被读取。

### 4.2 驱动事件日志（driver monitor）

驱动通过 UDP 127.0.0.1:28643 发送 `PANVK_G2_V1 ...` 事件和着色器 NIR blob。测试前在后台启动：

```sh
cd /root/mali-g2-project/tools
mkdir -p work/eden-testN/nir
python3 driver_monitor.py --output /storage/emulated/0/Download/panvk-g2-testN-driver.log \
  --capture-dir /root/mali-g2-project/tools/work/eden-testN/nir --seconds 28800 --sockets 16
```

每行是 JSON：`{"received":..., "event":"PANVK_G2_V1 pid= tid= t= <消息>"}`。测试结束后把 log 移到 `work/eden-testN/driver.log`。
驱动也会追加写 `Download/panvk-g2-driver-events.log`（≤8MB，仅当该文件已存在）。

关键事件：

| 事件 | 含义 |
| --- | --- |
| `device begin build=... threads_wg=...` | 确认 Eden 加载的是哪个构建（注意 SHA 可能因未重新生成而滞后，用 threads_wg 等字段辅助判断） |
| `fault crumbs done vt/frag/cs ... started ...` | 每个子队列最后完成/开始的工作：`serial<<32 | work 序号`；started>done 的那个就是卡住/出错的工作 |
| `fault submit=.. cmdbuf=.. serial=.. work=N` | 出错 submit 中的命令缓冲及其工作数 |
| `fault work=K draw/dispatch/copy/frag ...` | 每项工作：shader id、push 常量、拷贝地址、FBD/附件信息（`info` 位：level[0:8] layer[8:20] format[20:30] load[32:36] live[36] afbc[37]） |
| `fault res/baddesc/words` | 回读的资源表；`baddesc` = 指向非 live BO 的描述符 |
| `freed va=...` | 最近释放的 BO（判断 use-after-free） |
| `badrange kind=...` | 录制时检查到描述符/缓冲越界（带 backtrace） |
| `crash signal=... frame=N lib=libvulkan_panfrost.so off=0x...` | 进程信号；用符号版还原：`zstd -d -c tools/artifacts/symbols/testN-unstripped.so.zst > /tmp/x.so; llvm-symbolizer -e /tmp/x.so 0x<off>` |
| `desc/vsattr/vsbuf shader=..` | sync 模式下的描述符/顶点属性转储 |

着色器捕获文件：`work/eden-testN/nir/shader-<pid>-<id>-<stage>-<point>.bin`，用 `replay_nir` 打印 NIR。

### 4.3 Eden 日志（需 root）

```sh
B=/storage/emulated/0/Android/data/com.miHoYo.Yuanshen.nightly/files/log
cd /root/mali-g2-project/tools/work/eden-testN; mkdir -p eden_logs
timeout 60 /system/bin/sh -c "su -c 'cp $B/eden_gpu.log $B/eden_gpu.log.old.txt $B/eden_log.txt $B/eden_log.txt.old.txt /storage/emulated/0/Download/; dmesg > /storage/emulated/0/Download/dmesgN.txt'"
for f in eden_gpu.log eden_gpu.log.old.txt eden_log.txt eden_log.txt.old.txt dmesgN.txt; do
  [ -f /storage/emulated/0/Download/$f ] && mv /storage/emulated/0/Download/$f eden_logs/; done
```

- `eden_log.txt`：当前会话；`.old.txt` 是上一次会话（Eden 每次启动轮转，游戏会话常在 `.old.txt`）。已开启 Vulkan validation，`DebugUtilCallback` 行是验证层报错；`ReportLoss: Device loss occurred!` 是 DEVICE_LOST。
- `eden_gpu.log`：用户开启的详细 GPU 日志，含 `[Vulkan] vkCmdDraw*/vkCmdDispatch`、`[RenderPass] Begin/End renderArea=`、`[Pipeline] Bind`、`[Memory] Allocated`，可与驱动 work 序号按顺序对应。
- `gpu_crashes/` 一般为空。

### 4.4 内核日志（dmesg）

`su -c 'dmesg'`。关键行：
- `Unhandled Page fault ... at VA 0x...` + `TRANSLATION_FAULT ... READ ... Shader core N, lsc`：着色器/预载读到未映射地址；其后列出所有 GPU 映射（SAME_VA zone，`Imported UMM` 为 Eden 导入的 1280x720 交换链图像，每个 0x1d8a000）。
- `CS_FATAL ... CS_BUS_FAULT` / `CS_FAULT ... INSTR_INVALID_ENC`：通常是上面 page fault 的连带结果。
- `couldn't handle the OoM event`：tiler heap 用尽（test25 已修，`PANVK_G2_TILER_MAX_CHUNKS` 默认 1024）。

## 5. 当前问题与结论

用户报告的症状：Eden 进游戏后闪退（GPU page fault → DEVICE_LOST）；会动的动画窗口和滚动文字出现黑块（静态文字正常，黑块一直存在，与 AFBC 无关）；3D 不显示（原厂驱动在 Eden 里也不显示 3D，但在 Winlator/盖世游戏 可以）。

| 版本 | 改动 | 结果 |
| --- | --- | --- |
| test25 | tiler heap 上限 1024 chunk | 仍闪退 |
| test26 | 子队列 start/done crumbs，fault 时回读资源表 | 定位到 fragment 子队列卡住 |
| test27 | 记录每个 render pass 的 FBD 和附件信息 | fault 在一个 1280x720 render pass，读取 AFBC D24S8 深度平面的 RGB8 视图（meta copy）时越界数百 MB |
| test27 + `noafbc` | 属性禁用 AFBC | **能进游戏不闪退**；黑块仍在；3D 不显示 |
| test28 | v15 上 ZS 图像不用 AFBC；`maxComputeWorkGroupSize` 由内核报的 256 提升到 1024（本机实测 1024 正确，原厂也暴露 1024） | **闪退，比 test27+noafbc 差** |

test28 闪退分析（`work/eden-test28/`）：dmesg 有 `Unhandled Page fault at VA 0x7355E16D00`，随后 CSI1 `INSTR_INVALID_ENC`；Eden `ReportLoss`。fault 时的 work 列表里有大量 `dispatch shader=2855`，其 push 常量 `pc1=0x1000000010 / 0x2000000010 / 0x8000000020` 很像工作组尺寸，说明 1024 线程工作组改动后 Eden 换用了新的计算路径。可能原因（尚未验证）：
1. 1024 线程工作组在大 shader（寄存器多/有 spill/TLS）下不成立：内核报 256 可能是因为寄存器文件只够 256 线程跑满 64 寄存器的 shader。wg1024 测试 shader 很小，没有覆盖这种情况。
2. 非 ZS 图像仍是 AFBC，还有其他 AFBC 路径出问题（noafbc 时没闪退）。

建议下一步：
1. 先单独回退 1024 工作组（或按 shader 寄存器数限制，`work_reg_count > 32` 时只允许 ≤ 512/256），保留 ZS 无 AFBC，验证闪退是否消失；再用 `sync,noafbc` 对照。
2. 黑块：只出现在每帧变化的内容上 → 检查每帧上传/拷贝的纹理（buffer→image 拷贝、staging、image 视图格式重解释、`VK_FORMAT_A1B5G5R5` 等）；可在 `vulkan_draw`/crashprobe 用 >16x16 的非纯色图案测 copy→sample→render→copy 往返（`tools/build/crashprobe/tiles/` 已写好 shader：`sample.comp`、`quad.vert`、`coord.frag`，宿主程序还没写）。
3. 3D：原厂驱动在 Eden 同样不显示，先别当成 PanVK 独有 bug。与原厂相比 PanVK 缺：geometryShader、tessellationShader、multiViewport/shaderOutputViewportIndex、transformFeedback、16x MSAA、fragment_shader_barycentric、external_memory_host（见 `work/eden-test28/caps-*.txt`）。clip/cull distance、fillModeNonSolid 原厂也没有。fork 里已有 GS 仿真 `driver/src/panfrost/vulkan/panvk_nir_lower_gs.c`，但只在 v6/v7 启用；移植到 v15 是补齐 1.4 特性的主要工作。用户希望最终与原厂特性对齐。

历史已检查：描述符/动态/顶点/索引/间接缓冲地址越界、bound set、copy 地址、freed BO 复用、双队列、CPU cache flush、tiler reclaim、copy/present shader、SSBO 越界钳制（硬件正确）、顶点越界 fetch。空 LOAD/STORE 的旧测试不能排除预载错误，test32 已用实际 LOAD 绘制复现并修正，见本文开头。

## 6. 关键代码位置

| 位置 | 内容 |
| --- | --- |
| `driver/src/panfrost/vulkan/panvk_g2_trace.c/.h` | 事件发送、崩溃处理、`panvk_g2_check_range`、fault 时资源表/内存回读 |
| `driver/src/panfrost/vulkan/csf/panvk_cmd_buffer.h` | `g2_draws` 工作日志（每项 8×u64）、`panvk_g2_crumb/_start` |
| `driver/src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c` | sync 模式 fault 报告（搜 `fault crumbs`） |
| `driver/src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c` / `_dispatch.c` / `panvk_vX_cmd_meta.c` | 工作记录点 |
| `driver/src/panfrost/lib/kmod/kbase_kmod.c` | kbase 后端；live BO 表、freed ring、GPU 属性（含 1024 工作组覆盖） |
| `driver/src/panfrost/vulkan/panvk_image.c` `panvk_image_can_use_mod` | AFBC 选择（含 v15 ZS 禁用） |
| `driver/src/panfrost/vulkan/panvk_physical_device.c` | v15 tiler heap 参数、队列数 |
| `driver/src/panfrost/compiler/bifrost/valhall/va_pack.c` | v15 编码与 preload 映射 |
| `references/eden-upstream/src/src/video_core` | Eden 渲染器源码（查调用方式） |

## 7. 测试流程（与用户协作）

1. 构建、本地测试、提交、打包 testN 放入 Download（只留一个 ZIP），推送 GitHub。
2. 确认 `debug.mesa.panvk.debug` 为所需值，后台启动 driver monitor。
3. 用户在 Eden 安装 ZIP 并进游戏，反馈现象（中文）。
4. 按第 4 节收集 driver log、Eden 日志、dmesg 到 `tools/work/eden-testN/`，分析。
用户偏好：不要反复提问，自主决定并直接做；报告要说清楚是“修复”还是“诊断版”。
