# 风中的草地

打开 Forge 的“文件 → 打开场景”，选择 `assets/scenes/Wind-Meadow.redflag`。选中“草地”，展开“植被”，关掉“风吹植被”后可看清根部位置。调大“每米植株的摆幅”会让叶尖移动得更远。“重新读取配置”撤回本地未保存的参数，“保存植被配置”写回选定的 YAML。引用同一文件的其他草地可重新读取。位置和风的方向按实体的局部 X、Z 轴计算，可通过旋转实体调整地块朝向。

[独立游戏窗口实拍](game.png)来自 `RedflagForge.exe --play assets/scenes/Wind-Meadow.redflag --frames 100` 的 1920×1080 客户区，程序正常退出。960×540 的 [关风](calm.png)、[0.45 秒](wind-045.png)、[0.90 秒](wind-090.png) 和 [隐藏植被](hidden.png) 取自同一机位的 Forge 光照输出，固定了天空、相机、曝光和灯光。地面采用 Amal Kumar 的 Gravel Road，植株采用 tevy 的 Savanna Grass Patch，两组资源均为 CC0。[草地来源](../../../RedflagForge/assets/game/station/salvage-kit/sources.json) · [地面来源](../../../RedflagForge/assets/game/station/salvage/ground/sources.json)

渲染一株需要 4,464 个索引。逐丛版本以相同的 36 份矩阵分别录制 36 个绘制指令。实例化版本用一个 `vkCmdDrawIndexed` 画 36 份网格，两组另外各画一次地面。CPU 的计时包含场景几何收集、矩阵写入与图形命令录制，不含队列提交、等待、显示或后处理。GPU 使用 RenderDoc 的 EventGPUDuration 回放两份颜色与拾取帧。全部回放没有中、高级别警告。表中 P10–P90 为中间八成样本的区间。运行时草丛参与方向光动态阴影。GI 的静态遮挡模型不会随叶片重建。

| 绘制方式 | 绘制次数（含地面） | CPU 中位 / P10–P90（微秒，40 帧） | GPU 中位 / P10–P90（微秒，12 次回放） |
|---|---:|---:|---:|
| 逐丛 | 37 | 235.10 / 225.80–425.51 | 30.912 / 30.259–31.462 |
| 实例化 | 2 | 127.95 / 93.13–179.75 | 31.312 / 31.078–31.635 |

设备为 GeForce RTX 5070 Ti Laptop GPU，构建为 Debug x64。两组固定在 0.45 秒时刻。GPU 数字是草地与地面绘制事件之和。实例化节省了 CPU 录制时间，两种画法的 GPU 耗时很接近。测试覆盖 60,884 个可拾取的草地像素，整图编号逐像素一致。改变风的设置后，草根的世界坐标保持不变。0.45 秒姿态在一米模型高度处最大偏移 0.11746 米（模型缩到 0.42 倍）。[全部 CPU 采样值](record-cpu-us.csv) · [GPU 事件和两份帧 SHA-256](replay.json) · [图像与数值检查](pixels.txt)

## 复现

从仓库根目录运行。测试会读正式场景、往返保存配置，先跑颜色和实体编号，再跑实际光照图。仅做图像验证时执行第一行即可。

```powershell
./tests/run_scene_tests.cmd --foliage
$rd = (Resolve-Path tmp/tools/renderdoc/RenderDoc_1.47_64).Path
$env:VK_ADD_LAYER_PATH = $rd
$env:VK_INSTANCE_LAYERS = 'VK_LAYER_RENDERDOC_Capture'
./tests/run_scene_tests.cmd --foliage instanced "$rd/renderdoc.dll"
./tests/run_scene_tests.cmd --foliage separate "$rd/renderdoc.dll"
Remove-Item Env:VK_ADD_LAYER_PATH,Env:VK_INSTANCE_LAYERS
Start-Process "$rd/qrenderdoc.exe" -ArgumentList '--python tests/replay_foliage.py' -WindowStyle Hidden -Wait
```

逐丛组只在测试命令列表内展开矩阵。两份原始帧在本机 `output/captures/local/foliage/{instanced,separate}_capture.rdc`。源码见 [逐株姿态](../../../Redflag/src/Redflag/Renderer/Foliage.cpp)、[场景绘制](../../../Redflag/src/Redflag/Renderer/SceneRenderer.cpp)、[Vulkan 录制](../../../Redflag/src/Platform/Vulkan/VulkanDraw.cpp)。风的公式和两张对照图在[《渲染性能实测》](../../pdf/Redflag-Rendering-Performance.pdf)的“风中的草地”一章。
