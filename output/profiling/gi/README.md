# Sponza 的 Vulkan 光照实测

工作区基于提交 `e90c28b` 加入 Vulkan 计算实现，设备为 RTX 5070 Ti Laptop GPU，Debug/x64，Vulkan SDK 1.3.275.0，RenderDoc 1.47。光照与像素颜色由计算着色器生成。捕获来自独立离屏程序，编辑器使用同一套 GI 接口。

`SponzaGI_capture.rdc` 的事件 8 更新探针，20 将探针光照到表面，32 与 44 生成并复用直接光候选，56 与 68 生成并重接一次反射路径。`capture-*.png` 分别导出事件 20、44 和 68 的输出。`renderdoc.json` 留有 SHA-256、工作组、资源编号和九轮 GPU 事件计时，计时前预热一轮。

`metrics.json` 与 `trials.csv` 记录 160×90 试验，16 盏固定灯、平均材质反射率，四个种子各计算线性 RGB 均方误差。PNG 对直接光使用曝光 4，对一次反射使用曝光 20。CSV 的毫秒列包括 CPU 提交与等待显卡的时间。

`temporal.json` 记录旧版编辑器预览的 320×180 尺寸。这里使用 Sponza 实体的模型变换、UV 贴图与三盏场景灯。每种模式运行 24 帧，记录静止机位下相邻画面的变化，并验证视口缩放后返回原尺寸能否复现原图。

## 重新运行

在仓库根目录构建 Debug/x64 的 Redflag 后，在 PowerShell 执行下列命令。图像分析使用 Python、NumPy 和 Pillow。

```powershell
python tests/fetch_sponza.py
tests/run_gi_benchmark.cmd
python tests/analyze_gi.py
tests/run_gi_benchmark.cmd --temporal
python tests/analyze_gi_temporal.py
```

重新捕获时，把下面的路径设为 RenderDoc 1.47 的安装目录。回放读取各个事件结束时的缓冲区，最后一步生成 PNG。

```powershell
$rd = (Resolve-Path tmp/tools/renderdoc/RenderDoc_1.47_64).Path
$env:VK_ADD_LAYER_PATH = $rd
$env:VK_INSTANCE_LAYERS = 'VK_LAYER_RENDERDOC_Capture'
tests/run_gi_benchmark.cmd --capture "$rd/renderdoc.dll"
Remove-Item Env:VK_INSTANCE_LAYERS, Env:VK_ADD_LAYER_PATH
Start-Process "$rd/qrenderdoc.exe" -ArgumentList '--python tests/replay_gi.py' -WindowStyle Hidden -Wait
python tests/analyze_gi.py
```

Sponza 原模型由 Marko Dabrovic 制作，Crytek 版本由 Frank Meinl 重制，PBR 纹理由 Alexandre Pestana 提供。本项目使用 Khronos 的固定 glTF 版本，自行构建了三角形空间索引、相机和灯光。[来源与署名](https://github.com/KhronosGroup/glTF-Sample-Assets/blob/e124e8d8d7330a5372bc2b4d00d39a4a9d7997ea/Models/Sponza/README.md) · [模型许可](https://github.com/KhronosGroup/glTF-Sample-Assets/blob/e124e8d8d7330a5372bc2b4d00d39a4a9d7997ea/Models/Sponza/LICENSE.md)
