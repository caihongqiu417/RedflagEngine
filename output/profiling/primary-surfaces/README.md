# 浴室的主表面

两次独立捕获均在 Windows、Debug x64 的 RedflagForge Vulkan 编辑器，使用 GeForce RTX 5070 Ti Laptop GPU，记录同一 RTXGI 浴室第 12 帧，1391 × 892 像素，镜头与时间抖动采样一致。旧轨迹的工作参数中 `stage=0, frame=11` 是该帧主射线计算；其前一道计算的 `stage=11` 是保留上一帧表面。两种路径均排除了这项历史操作。新帧计算着色器第 459 号事件接收本帧的相机矩阵逆与两张浮点颜色图。

在根目录用 RenderDoc 1.47 运行下面的命令。脚本在程序退出前保存 `latest-ms.json`；发布记录 `summary.json` 保留原样。捕获较大，存在本地 `output/captures/local`，文件名与 SHA-256 见 [summary.json](summary.json)。两份捕获分开预热，并在同一次回放会话中各取 11 次 `EventGPUDuration`。原始事件计时另见 [replay.json](replay.json)。

~~~powershell
qrenderdoc --python tests/replay_primary_surfaces.py
~~~

各次记录仅统计表中列出的 GPU 事件；清图、提交间隙、CPU 驱动开销、窗口与后续光照另计。新版与旧版对法线和贴图的采样方式不同，不能把数值当成整帧提升或像素完全等价的结果。
