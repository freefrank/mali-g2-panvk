# 当前状态

## 目标设备

- 设备：Xiaomi M367FC，codename `yingtian`
- SoC：XRing O3
- GPU：Mali-G2-Ultra-NX r0p1 MC16
- 架构：Valhall v15，raw GPU ID `0x0f080600f0000103`
- 系统：Android API 37，Termux + proot Manjaro ARM64
- 内核接口：`/dev/mali0` 可读写，Kbase 1.39
- 原厂 Vulkan：r56p0-15bet0，Vulkan 1.4.345，subgroup 16

## 当前版本

- 驱动仓库 HEAD：`72569ca`，分支 `main`
- 工具仓库整理前基线：`8efa308`，分支 `g2-bringup`；整理提交以 `git log` 为准
- 最新 ZIP：`/storage/emulated/0/Download/panvk-mali-g2-test7-72569ca.zip`
- ZIP SHA-256：`e127fbe9164cbae6d18f072eac82bccc9bed10a3f5ff3bf2eba53809f1e91620`
- 结论：test7 是诊断版本，Eden 仍在启动游戏时退出。

## 已验证能力

- Mali-G2 64 位 GPU ID 识别。
- v15 指令编码转换与计算着色器执行。
- 基础图形、纹理、离屏渲染与 Android Hardware Buffer 导入。
- 顶点着色器存储与原子操作。
- Fragment XY/front-face 预载入。
- DrawID 的 G2 R3 预载入映射。
- Eden 使用的 7 个计算着色器可编译。
- ASTC void-extent 和 3 组普通块与原厂输出逐字节一致。
- test7 捕获的 4964 个后端 NIR 输入在 Android 真实编译器对象中以两个线程批量重放，全部通过。

## Eden 退出调查结论

test7 记录了 2488 次管线编译开始、2486 次结束，没有编译器错误，也没有 GPU submit。捕获的 4964 个着色器输入全部可以独立重放，因此没有复现单个着色器后端崩溃。

另一个确定问题是 PanVK 只报告并分配 16 个顶点属性/绑定，而原厂驱动和 G2 硬件实际支持 32 个：

- 原厂驱动：32 属性 + 32 独立绑定，30/30 帧通过，256/256 像素正确。
- 原厂驱动：32 属性 + 1 个绑定，30/30 帧通过。
- test7 强制 32 属性 + 1 个绑定：渲染错误。
- test7 强制 32 个绑定：`SIGSEGV`，退出码 139。

PanVK release 构建中的 `MAX_VBS=16` 会使第 17–32 个绑定越界写命令缓冲区状态。这个问题与 Eden 的退出形态一致，但按当前要求已停止修复；源码未包含该修复。

对应固定证据位于 `fixtures/vertex-input32-result.json`，测试着色器位于 `fixtures/vertex_input32.vert` 和 `fixtures/vertex_input32.frag`。

## 尚未补齐

- 32 个顶点属性和绑定的 PanVK 状态、描述符表及能力上报。
- `fillModeNonSolid`
- `shaderClipDistance` / `shaderCullDistance`
- geometry shader / tessellation shader
- multi-viewport（当前最大 1）
- geometry streams / transform feedback 的 CSF 路径
- 部分 v15 fragment preload 映射仍未知

不能只翻转 Vulkan feature bit；每项都需要实际 G2 GPU 测试。

Policy marker: biblioklept
