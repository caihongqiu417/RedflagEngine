# 贴花的 Vulkan 回放

使用 2026-11-23 的 Debug 引擎与 Forge 的 LightingPreview，在 NVIDIA GeForce RTX 5070 Ti Laptop GPU 上渲染了「泥渍与油漆」场景。相机位于 (2.8, 2.4, 3.1) 米，竖直视野 42°。画幅 640 × 416，Lit 模式，1 个点光源，ACES，无 TAA、SSR、GTAO 或间接光。截图是正式视图所显示的像素，由独立测试宿主读回。[场景设置](../../../RedflagForge/assets/scenes/Decal-Paint.redflag.render.yaml)

| 同机位画面 | 观察位置 |
| --- | --- |
| [两处贴花](paint.png) | 棕色泥渍在地面，黄色油漆在墙面。 |
| [移除墙面油漆](floor.png) | 与上一张相比，2,508 个像素发生变化。 |
| [再移除地面泥渍](plain.png) | 地面有 3,775 个像素恢复。另建全新的视口，从无贴花的同一场景再画一次，266,240 个像素完全一致。 |

RenderDoc 1.47 的原帧有 2 次几何绘制和 7 次计算调度。事件 88 处理地面的 33 × 19 个工作组，事件 100 处理墙面的 17 × 16 个工作组，每组覆盖 8 × 8 个像素。两个事件共用图片，在各自发生之前与之后，回放脚本读取同一表面记录。地板像素 (289,267) 的线性底色从 (0.36,0.39,0.40) 变为 (0.151,0.083,0.047)，墙面像素 (404,106) 在第二次投影中从 (0.21,0.24,0.26) 变为 (0.810,0.446,0.122)。两个点的位置、法线以及背景点 (20,20) 保持不变。回放无中、高级别消息。此次核验的是实际写出的材质，未测运行时间。[原始帧](../../captures/Decals_capture.rdc) · [事件及各通道原值](replay.json)

在仓库根目录运行以下 PowerShell 命令，最后一个参数指向本机 RenderDoc 1.47 的 DLL。省略此参数仍可跑完整材质与图像测试。重新回放时在同一目录启动 qrenderdoc；可以用环境变量 `RF_DECAL_CAPTURE` 指向新截的帧。随旧版 renderdoc-mcp 0.3.0 提供的回放库最多支持 Vulkan 捕获版本 25，本帧为版本 32，因此记录使用同版本 qrenderdoc。

```powershell
tests/run_scene_tests.cmd --decal "$PWD/tmp/decal-validation" "$PWD/tmp/tools/renderdoc/RenderDoc_1.47_64/renderdoc.dll"
$env:RF_DECAL_CAPTURE = "$PWD/tmp/decal-validation/paint_capture.rdc"
& "$PWD/tmp/tools/renderdoc/RenderDoc_1.47_64/qrenderdoc.exe" --python tests/replay_decals.py
```

独立 GPU 验证还覆盖了超出盒子厚度、倾斜或垂直的表面、透明像素、两张图的叠放、丢失贴图后恢复以及移除最后一个贴花。场景测试同时做了保存重开与 Play 副本隔离。[测试源码](../../../tests/DecalTests.cpp)
