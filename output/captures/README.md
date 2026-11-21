# 深度预写与模型绘制的 RenderDoc 截帧

`DepthPrepass_capture.rdc` 来自真实 OpenGL 调用捕获。对象是 `DepthPrepassTests` 的 32×32 离屏回归用例，不是整个编辑器场景。

用例把基础颜色贴图的 alpha、独立透明度贴图的 R 通道及 0.75 材质系数相乘。它先执行深度预写，再清理目标、独立绘制主画面，逐像素比较两者的可见区域。预期第 0–7 和 24–31 列可见。截帧中保留 `Depth prepass: alpha coverage` 与 `Main pass: independent alpha coverage` 两个调试分组。

捕获使用 Rider 自带的 RenderDoc 1.33.0.0 DLL，通过官方 1.6.0 应用接口开始和结束。`GetNumCaptures` 返回 1，`EndFrameCapture` 和两次 `GetCapture` 均成功。设备为 NVIDIA GeForce RTX 5070 Ti Laptop GPU，OpenGL 4.6.0，NVIDIA 驱动 616.56。原始测试输出见 `capture.log`，文件大小与 SHA-256 见 `manifest.json`。

在项目根目录复现：

```powershell
.\tests\run_depth_tests.cmd --capture "D:/ProgramFiles/JetBrains/JetBrains Rider 2025.3.1/plugins/rider-plugins-renderdoc/runtime/windows-x86_64/renderdoc.dll"
```

DLL 必须在创建图形上下文前加载，测试入口已保证这个顺序。不带参数运行时不会加载 RenderDoc：

```powershell
.\tests\run_depth_tests.cmd
```

默认路径和开启捕获的路径都已通过逐像素断言。随后用官方便携版 RenderDoc 1.47 打开保存的 RDC，开启 API validation，独立回放并检查附件。工具来源为 [RenderDoc 官方 1.47 ZIP](https://renderdoc.org/stable/1.47/RenderDoc_1.47_64.zip)，下载记录位于 `tmp/tools/renderdoc/download.json`。

在项目根目录回放现有捕获，不重新运行引擎：

```powershell
& .\tmp\tools\renderdoc\RenderDoc_1.47_64\qrenderdoc.exe --python "$PWD/tests/replay_depth_capture.py"
```

脚本调用 `OpenCapture`、`SetFrameEvent`、`GetTextureData`、`PickPixel` 与 `SaveTexture`。完成后退出自己的进程，不操作已经打开的 RenderDoc 窗口。检查 `replay-results.json` 的 `status`：本次为 `passed`。该文件保存逐项状态、全部驱动消息、三个像素样本和整张附件的检查结果。

| 检查 | EID 28：深度预写 | EID 52：独立主绘制 |
| --- | --- | --- |
| 索引数 | 6 | 6 |
| 深度测试／深度写入 | 开启／开启，LessEqual | 开启／开启，LessEqual |
| 三个颜色附件的写掩码 | 0、0、15：只写法线附件 2 | 15、15、15 |
| 深度附件 60 | 512 个 0.5，512 个 1.0 | 与 EID 28 逐字节相同 |
| 实体附件 58 | 1024 个 -1 | 512 个 42，512 个 -1 |

每个事件都检查了 32×32 的全部 1024 个像素。按回放 API 的左上角坐标，在 `(4,16)`、`(12,16)`、`(28,16)` 取样，两事件的深度均为 `0.5、1、0.5`；实体 ID 在预写时均为 `-1`，主绘制时为 `42、-1、42`。两次绘制实际绑定相同的资源：基础颜色贴图 67 的 alpha 为 `[255,64,255,255]`，透明度贴图 68 的 R 为 `[255,255,64,255]`；材质缓冲 55 的 64 字节中，基础 alpha 为 0.75、两张贴图开关为 1、Mask 为 1、阈值为 0.5。绑定位置分别为 3、10、2。

`renderdoc-eid28-depth.jpg` 是实际 RenderDoc 1.47 窗口截图，选中 EID 28 后查看深度附件。它与下面的附件导出图来源不同：

- `prepass-depth.png`、`main-depth.png` 是回放 API 直接导出的深度图，灰度 127 对应深度 0.5，白色 255 对应清屏深度 1。
- `prepass-entity.png`、`main-entity.png` 是回放 API 以 SInt 导出的实体附件。本次 PNG 中实体 42 显示为灰度 42，清屏值 -1 显示为白色 255；不是线性范围归一化。
- `prepass-entity.dds`、`main-entity.dds` 保留 R32_SINT 原始整数。数值断言直接读取回放附件，不依赖 PNG 显示亮度。

捕获记录含 14 条缓冲分配 Info 和 1 条性能提示：EID 28、NVIDIA 消息 131218、Medium，内容是顶点着色器根据 GL 状态重新编译。本次开启验证的回放新增 3 条缓冲 Info，无新增警告或错误，`GetFatalErrorStatus` 为 Success。捕获与回放消息在 JSON 中分开保存。驱动重编译提示需要用代表性场景另行测量，不能据此给出性能损失数值。

`mcp-check.json` 另存 `renderdoc-mcp v0.3.0` 的实际查询：两个分组各有一次 6 索引绘制，事件为 28 和 52。MCP 的 `get_log` 返回空列表；它不能代表捕获中没有驱动消息，驱动记录仍以 `replay-results.json` 为准。

额外尝试通过独立 RenderDoc 的 `--ui-python` 选择 EID 52 并抓取窗口，事件、附件和选点均设置成功，但离屏 `QWidget.grab` 未包含原生 GPU 视图区，`QScreen.grabWindow` 得到黑图。失败记录见 `gui-capture.json`；没有将这些图片作为截图证据。

这些记录用于检查绘制顺序、参数和附件，不是性能基准。程序耗时、截帧耗时和日志中的 Shader 编译耗时均不能当作 GPU 绘制耗时。
