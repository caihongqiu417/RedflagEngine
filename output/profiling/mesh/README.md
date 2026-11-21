# 64 个四边形的三种画法

固定一张 256 × 256 的图，将 64 个不透明方块排成 8 行 8 列。所有方块使用同一材质、同样的四个顶点和六个索引，位置和拾取编号各不相同，相邻方块之间留有空隙。第一帧对照使用 64 个内容相同但独立创建的网格，第二帧共享一份网格，第三帧在第二帧的基础上增加深度预写。第一、第二种画法的差别也包括网格、VAO 和逐物体数据的使用方式，用它们比较引擎当前两条完整的绘制路径。

## 帧里的绘制

| 打开的帧 | 在 RenderDoc 中选中的绘制 | 主绘制的片元着色器调用 | 预写的片元着色器调用 |
| --- | --- | ---: | ---: |
| [separate_capture.rdc](separate_capture.rdc) | EID 24–465，64 次普通绘制，各用 6 个索引 | 43,264 | — |
| [instanced_capture.rdc](instanced_capture.rdc) | EID 44，一次绘制使用 6 个索引和 64 个实例 | 43,264 | — |
| [prepass_capture.rdc](prepass_capture.rdc) | EID 26–467，64 次深度预写。EID 503，一次绘制 64 个实例 | 43,264 | 43,264 |

在第三帧的 EID 467 查看深度附件，会看到 64 个灰色方块。方块的深度为 0.5，白色间隙为清屏值 1.0。预写时，深度比较是 LESS，允许写入深度，颜色和实体编号禁止写入，法线图开启写入。到 EID 503，主绘制改用 LEQUAL 并关闭深度写入。打开结果图，所有方块都已填白。三份帧的最终颜色、实体编号和深度原始字节分别完全相同，覆盖的像素为 43,264，背景为 22,272。RenderDoc 图像从顶行开始计数，位置 (16,15) 的编号为 56，(16,239) 为 0，(240,239) 为 7，间隙 (31,31) 为 −1。[回放导出的预写深度](prepass-depth.png) · [最终颜色](final-color.png) · [EID 503 事件浏览器截图](renderdoc-events.png)

表中的片元数来自 RenderDoc 回放时收集的 PS Invocations。每个阶段由 128 个三角形覆盖相同的 43,264 个样本，Samples Passed 也记录了这个数。方块互不重叠，这次预写多执行了 64 次绘制，对后面主绘制的片元数没有贡献。引擎的预写还提供法线与深度给 SSAO、RSM，这个小场景没有运行这些效果。[每个事件、计数器、附件哈希与驱动消息](replay.json)

## 独立运行的耗时

2026-11-21 在同一机器上用 Debug x64 运行同一组几何。设备为 NVIDIA GeForce RTX 5070 Ti Laptop GPU，驱动报告 OpenGL 4.6.0 NVIDIA 616.56。捕获与回放用 RenderDoc 1.47。采样时以普通进程启动，没有加载 RenderDoc，每种画法各预热 30 帧、采集 120 帧。三种画法逐轮轮换执行顺序，每次取回查询结果后再画下一帧。CPU 用单调时钟测一次绘制提交，GPU 用 GL_TIME_ELAPSED 查询，两项都包含目标图的清除。取回查询、验证像素与写 CSV 的时间在测量区间外。

| 绘制方式 | CPU 中位数 / P95（μs） | GPU 中位数 / P95（μs） |
| --- | ---: | ---: |
| 64 个独立网格 | 282.10 / 459.10 | 8.384 / 9.184 |
| 共享网格的 64 个实例 | 145.30 / 291.80 | 6.912 / 8.096 |
| 先深度预写，再画实例 | 318.95 / 582.90 | 9.248 / 13.152 |

中位数取排序后中间两项的均值，P95 取排序后第 114 个值。共享网格后，CPU 提交的中位数减少 136.80 μs。从实例绘制增加预写后，GPU 中位数增加 2.336 μs。这个场景分辨率小且没有遮挡，数据描述这一场景在这台机器上的结果。[360 个原始样本，单位 ns](timing.csv)

## 复查与下一项实验

运行目录为仓库根目录。下面用仓库内本次实际运行的工具位置举例，也可以填入本机 RenderDoc 的路径。三个模式分别从十个预热帧之后截一帧。再次采集前另存需要保留的同名 RDC，以便将回放对准新采集的三份文件。

```powershell
$rdRoot = (Resolve-Path tmp/tools/renderdoc/RenderDoc_1.47_64).Path
$dll = Join-Path $rdRoot renderdoc.dll
tests/run_profile_tests.cmd --capture separate $dll
tests/run_profile_tests.cmd --capture instanced $dll
tests/run_profile_tests.cmd --capture prepass $dll
& (Join-Path $rdRoot qrenderdoc.exe) --python tests/replay_mesh_comparison.py
tests/run_profile_tests.cmd --measure output/profiling/mesh/timing.csv
```

采集时引擎基于 `054826d`，包含本次测试夹具修改。链接的 Debug 引擎静态库 SHA-256 为 `74071eefe21241c69e272edeb46faa418b073686c098c980ed1df601b851304a`。RDC 的 SHA-256 记录在 replay.json。此次用 renderdoc-mcp v0.3.0 打开三份帧，核对了阶段与绘制事件，再用 ReplayController 读取深度状态、像素、GPU 计数和完整附件。

下一次可沿相机方向叠放相同网格，固定可见面积与光照，逐级增加遮挡，成对比较预写开关后的主绘制片元数、总 GPU 耗时以及画面。若 64 次深度绘制占主要开销，再将符合条件的不透明物体按网格实例化预写，单独核对遮挡、透明裁剪、法线输出。驱动还记录了着色器重编译以及缓冲移动的提示，后续可固定预热与容量，分别测试实例数据上传及首次使用的开销。[测试夹具](../../../tests/ProfileTraceTests.cpp) · [回放检查](../../../tests/replay_mesh_comparison.py)
