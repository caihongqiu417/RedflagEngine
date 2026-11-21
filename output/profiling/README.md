# Redflag 渲染性能记录

2026-11-21 在两项离屏实验中采集了真实 RenderDoc 帧，并在单独运行中分别测了 CPU 提交和 GPU 命令区间。[2D 精灵](batch2d/README.md)比较逐个提交与合批，[3D 网格](mesh/README.md)比较独立网格、共享网格实例化，以及加上深度预写的开销。每组的捕获帧、回放结果和原始计时保存在各自目录。[渲染性能实测](../pdf/Redflag-Rendering-Performance.pdf)汇总数据、画面和后续验证方向。

## 旧版 CPU 时间线

下文是同日较早的专项 CPU 采样，只涉及原来的测试配置。Forge 和 Sandbox 的性能文件另行保存。

`redflag-cpu-trace.json` 是项目 `Instrumentor` 直接写出的原始 Trace Event JSON，未修改事件或耗时，共 300 个事件、一个 CPU 线程，大小约 39 KB。文件只含函数名称、时间和本次线程的哈希编号，没有账号、用户目录或资产路径。

两张 PNG 来自 Chrome 157.0.8103.79 自带的 DevTools Performance 页面。使用独立的浏览器数据目录，通过 Chrome 的远程调试接口导入原始 JSON，再由浏览器截图；没有使用用户的浏览器 Profile，没有上传记录，也没有重画图表。Chrome 当前默认关闭 `chrome://tracing` 调试页，因此改用 DevTools 的 Performance 导入功能。

## 运行环境与采样次数

- Windows 11，系统版本 10.0.26200；AMD Ryzen 9 8945HX。
- NVIDIA GeForce RTX 5070 Ti Laptop GPU，OpenGL 4.6.0，驱动 616.56。
- Debug/x64，C++17；隐藏 GLFW 窗口，256×256 绘制目标。
- 8×8 排列的 64 个不透明四边形，共享同一网格和材质。深度预传递逐个绘制，主绘制使用现有实例化路径。
- 无场景灯光与 IBL，SSAO 输入固定为白色，RSM 输入固定为黑色；没有阴影、后处理、编辑器界面或深度预览读回。
- 初始化及 10 帧预热写到 `bin-int/Tests/ProfileTrace-warmup.json`；之后 60 帧写入本目录的原始记录。
- 测量区间结束后，读回实体 ID，检查全部 64 个四边形中心的 ID 正确且没有 OpenGL 错误。读回不包含在采集区间内。

每帧依次记录 `CPU submission - 64 opaque quads` 和 `CPU wait - glFinish`。外层 `Frame` 包含这两段，以及嵌套计时记录写入所花的时间。渲染器原有的 `ExecuteDepthPrepass` 与 `EndScene` 计时也进入同一记录。

## CPU 时间线中的绘制与等待

完整统计见 `statistics.json`。以下每项都有 60 个样本，单位为微秒。

| CPU 区间 | 中位数 | 最小值 | 最大值 |
| --- | ---: | ---: | ---: |
| 提交这一帧 | 347 | 303 | 842 |
| 深度预传递调用 | 199 | 188 | 666 |
| 主绘制 EndScene 调用 | 19.5 | 16 | 52 |
| glFinish 等待 | 66.5 | 33 | 1810 |
| 外层 Frame | 467 | 359 | 2196 |

`chrome-cpu-prepass.png` 选中第 21 个采集帧的深度区间，对应原始事件索引 100，持续 192 微秒，Chrome 四舍五入显示 0.19 ms。`chrome-cpu-wait.png` 选中同帧的等待区间，对应事件索引 103，持续 94 微秒。索引从 0 开始；Chrome 显示为 `r-100` 和 `r-103`。截图只展示一个区间，统计由原始 JSON 的全部样本计算。

这些值属于一次带计时记录的 Debug 运行。`Instrumentor` 使用 CPU 的 `high_resolution_clock`，转换为微秒，每个事件立即写入并 flush 文件，会影响外层区间。CPU 提交包含驱动调用，也可能发生隐式等待；glFinish 是 CPU 等待已经提交的显卡工作完成，不能当作 GPU 的准确执行时间。

每帧 glFinish 会让 CPU 和 GPU 串行，采集没有交换窗口缓冲，也没有完整编辑器工作负载。因此不能用这些值换算编辑器帧率，不能推断哪个效果一定是瓶颈，也不能声称这轮改动带来了速度提升。未采集 Release、GPU 时间查询、不同场景、功耗模式或前后对照实验。

## 重新采集时间线

先构建 RedflagForge Debug/x64，然后在仓库根目录执行：

```bat
tests\run_profile_tests.cmd
python tests\summarize_profile.py output\profiling\redflag-cpu-trace.json > output\profiling\statistics.json
```

脚本只重写本测试的 trace 和预热记录。重复采集会产生新的测量结果，应重新生成统计并更新截图，不继续沿用旧数值。与其他 GPU 测试顺序运行，避免同时写 Shader 缓存。

在 Chrome 打开开发者工具的 Performance 面板，点击 `Load trace…` 选择 JSON。展开 `Process (0)` 和其下线程，滚轮缩放时间轴，选择 `ExecuteDepthPrepass` 或 `CPU wait - glFinish`。下方 Summary 可核对名称、持续时间和原始事件。这个文件不是网页性能录制，页面里的网页专属指标不适用于 Redflag。

实现入口是 `tests/ProfileTraceTests.cpp` 和 `Redflag/src/Redflag/Debug/Instrumentor.h`。复现统计脚本不依赖第三方 Python 包。截图与 trace 哈希记录在 `manifest.json`。

参考：[Chrome Performance 导入与分析](https://developer.chrome.com/docs/devtools/performance/reference#load)、[Chrome Headless](https://developer.chrome.com/docs/automation-and-testing/headless)、[Khronos glFinish](https://registry.khronos.org/OpenGL-Refpages/gl4/html/glFinish.xhtml)。
