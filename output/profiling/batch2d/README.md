# 64 个精灵的提交与合批

同一个 8×8 网格绘制两次。每个格子放一个精灵，黄色菱形和蓝色十字交错排列。图标使用两张 9×9 贴图，透明部分可以看到背景。两次都走项目的 `Renderer2D::DrawSprite`。逐个提交时，每画一个精灵就调用一次 `BeginScene / EndScene`。合批时，64 个精灵共用一次调用。因此计时差值包含更新相机、上传顶点、绑定纹理和提交绘制的成本。

本次使用引擎版本 `054826d` 的 Debug x64 库与[测试夹具](../../../tests/SpriteBatchProfile.cpp)，目标为 256×256 的 RGBA8 颜色图和 R32I 实体编号图。设备是 NVIDIA GeForce RTX 5070 Ti Laptop GPU，驱动 616.56，OpenGL 4.6。采集与回放使用 RenderDoc 1.47。

## 绘制与图像

在 [Sprites_capture.rdc](Sprites_capture.rdc) 中打开事件浏览器，选择下表的绘制。部分完成的图来自第一组的第 32 次绘制。最后一次绘制之后，从两种方式的颜色图和实体编号图各读回全图，65,536 个像素的两份 RGBA8 和两份 R32I 分别逐字节相同。共有 64 个不同实体编号，背景的编号为 −1。纹理哈希、取样颜色和捕获消息可在[回放记录](replay.json)中复查。RenderDoc MCP 也独立列出了[两组绘制事件](mcp.json)，[事件浏览器截图](renderdoc-events.png)保留了逐个绘制的结尾和合批的那一次绘制。

| 捕获区间 | 绘制 | 索引与三角形 | 颜色图 |
| --- | --- | --- | --- |
| 逐个提交，EID 13–643 | 64 次，每次传入 4 个顶点 | 每次 6 个索引，共 128 个三角形 | [EID 323，完成前 32 个精灵](individual-half.png) · [EID 643，全部完成](individual.png) |
| 一次合批，EID 658 | 1 次，传入 256 个顶点 | 384 个索引，共 128 个三角形 | [EID 658，全部完成](batch.png) |

## 单独运行的计时

关闭 RenderDoc 注入后，用同一台机器和同一个夹具测量上述两种画法。每种先预热 30 帧，再测 120 帧，每一轮交换先后顺序。CPU 计时包含清除两个目标、准备数据和向 OpenGL 提交绘制。GPU 使用 `GL_TIME_ELAPSED` 记录这一段命令的执行间隔，其中也可能包含显卡等待 CPU 继续提交的时间。等待查询结果在 CPU 计时区间之外。测量不包含窗口呈现。P95 取 120 条数据从小到大第 114 条；每一轮的原始纳秒值见 [timing.csv](timing.csv)。

| 场景 | CPU 中位数 / P95（µs） | GPU 中位数 / P95（µs） |
| --- | --- | --- |
| 逐个提交 | 959.950 / 1418.700 | 183.696 / 201.408 |
| 一次合批 | 226.900 / 432.600 | 3.072 / 4.608 |

两种画法都提交 384 个索引。这个例子中，重复建立每个小批次占去了较长时间。捕获中的 EID 13 还有驱动关于动态顶点数据从显存移往主存的提示。要判断流式上传是否值得改，可在优化构建中保持相机更新次数相同，分别计写顶点与提交绘制，并改变每批精灵数和贴图数。记录画面已覆盖的像素量，另测越过贴图容量时多拆了一次批的开销。

## 复现

在仓库根目录执行以下命令，路径换成本机的 RenderDoc 安装位置。运行脚本使用现有 Debug x64 的引擎、GLFW 和 Glad 库。

```powershell
.\tests\run_sprite_batch.cmd --capture "D:\Tools\RenderDoc\renderdoc.dll"
& "D:\Tools\RenderDoc\qrenderdoc.exe" --python "$PWD\tests\replay_sprite_batch.py"
.\tests\run_sprite_batch.cmd --measure
```
