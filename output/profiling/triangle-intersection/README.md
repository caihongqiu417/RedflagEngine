# 三角形共边、阴影与实时探针

Sun Temple 和 Sponza 的 PNG 均由正式 Vulkan 编辑器第 24 帧的场景输出图导出。图中可见的雾来自场景的八步散射计算。保留场景文件中的主相机、材质和点光源，打开逐灯直接光和实时 DDGI。测试场景的私有副本省略烘焙探针，关闭 TAA、SSR、GTAO 和移动时简化光照。画面大小固定为 320 × 205。两套模型与许可见 [Epic Games / NVIDIA ORCA · Sun Temple](https://developer.nvidia.com/ue4-sun-temple) 和 [Khronos glTF Sample Assets · Sponza](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Sponza)。

## 捕获和回放

需要工程的 Debug x64 运行依赖、Windows、pywin32、PyYAML、NumPy、Pillow 和 RenderDoc 1.47。场景需先按 `tests/fetch_sponza.py` 与 `tests/fetch_sun_temple.py` 的来源准备模型。以下 PowerShell 命令在仓库根目录运行。若 RenderDoc 安装在别处，在截帧命令后加 `--renderdoc` 和包含 `qrenderdoc.exe` 的目录。

```powershell
$previous = 'tmp/emissive-profile/RedflagForge-shared-depth.exe'
$projected = 'tmp/shared-edge/RedflagForge-ee2627d7c42f.exe'
$final = 'tmp/shared-edge/RedflagForge-d0180005a4f6.exe'
python tests/capture_intersection_scenes.py --exe $previous --reuse --measure
python tests/capture_intersection_scenes.py --exe $projected --reuse --measure
python tests/capture_intersection_scenes.py --exe $final --reuse --measure

# 按版本顺序读取回放报告，比较每两版的 RGB，导出最后一版的两张实机图。
python tests/summarize_intersection_scenes.py $previous $projected $final --tree-plans
```

每个版本从独立 Windows 桌面打开正式编辑器，RenderDoc 保存第 24 帧。加 `--scene Sun-Temple` 或 `--scene Sponza-Atrium` 可以只测一个机位。`--reuse` 校验可执行文件摘要、场景内容和渲染设置，沿用同一份捕获。另有候选时，将它也传给截帧命令，再把它作为汇总命令的第三个文件。完整帧存放在本机 `output/captures/local/` 下，捕获的相对路径和 SHA-256 见 `renderdoc.json`。

## 读数

`991fdf640a3d` 是用两个重心坐标判断三角形内部的旧版。`ee2627d7c42f` 将三角形投到与射线对齐的平面，按共边计算两个相反的方向。在 Sun Temple，该实现将探针更新从 1.176 毫秒增至 2.259 毫秒，Sponza 从 1.189 增至 2.139 毫秒。同机位下旧版和这一版本的直接光 RGB 完全一致。雾的输入 RGB 恰好相同的像素分别有 6,552 和 1,459 个，这些像素经过雾后的颜色也逐一相同。

`d0180005a4f6` 保留共边判断，每条射线只准备一次投影矩阵，并按包围盒面积及三角形数量从 12 段中选择分界。相较于旧版，神庙的雾从 4.402 降至 2.554 毫秒，探针更新从 1.176 增至 1.472 毫秒；Sponza 的雾从 3.666 降至 2.571 毫秒，探针更新从 1.189 降至 1.035 毫秒。最终版与旧版的逐灯光照均逐像素相同，转成 8 位面板图后，神庙有 8 个像素相差一级，Sponza 有 6 个。

两幅 `tree-*.png` 均从对应版本的 Sun Temple 帧读回。着色器读到的三角形重心投在水平面上形成灰色底图，颜色深浅随每格内的重心数增加。两个矩形按 GPU 上传的包围盒端点绘制，黑圆点表示场景中的主相机。按数量平分时两个子树各有 303,188 个三角形；改为按左右子盒的面积和数量比较后，两组分别有 538,079 和 68,297 个。建树所花的 CPU 时间与记录方法见 [CPU 建树实验](cpu-build.md)。

`renderdoc.json` 按场景和可执行文件 SHA-256 的前 12 位分组，记录三角形数、BVH 节点数、光源数、探针网格、每道计算的事件、工作组数、输入参数和输出摘要。NVIDIA GeForce RTX 5070 Ti Laptop GPU，开启 Vulkan 验证。每份捕获先完整采集一次 GPU 时间计数器预热，再采集 11 次。每个 `gpu_ms` 数组都是同一事件的毫秒数。`captured_cpu_dispatch_us` 是录制这一帧时 RenderDoc 记录的单次 Vulkan 调用时间，单位微秒，包括截帧开销。两种计时分别回答 CPU 花多久将命令写进命令缓冲区、GPU 执行它又要多久。

`pixels.json` 保留线性 RGB 三个通道的平均绝对差、最大差、超过 0.0001 的像素或探针方向数。该阈值便于逐版比较，不是几何命中判据。坐标从导出图的左上角开始。探针数组含 504 个探针，每个探针有六个方向，按每帧每方向两条射线取样。雾的输入已经合成直接光和间接光，两版之间的最终色差也包含这部分差异。面板图经过编辑器的曝光与显示转换，再将已转换的 RGB 乘 255、舍入到最近整数生成 PNG。

共边是否漏光另见 `tests/run_gi_benchmark.cmd --triangle-edge` 及 [逐条射线的受控实验](../emissive-gi/edge-measurement.json)。沿射线把三角形投到平面，再用三条边的方向判断是否命中，可参考 [PBRT 第四版 §6.5，Triangle Meshes](https://pbr-book.org/4ed/Shapes/Triangle_Meshes)。

另用 Sponza 的 14,256 个可见表面像素检查七种光照方法、四个随机种子，逐项核对误差与非有限数，详见 [GI 回归记录](gi-regression.json)。
