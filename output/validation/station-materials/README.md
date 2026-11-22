# 维修站的三件材质

打开游戏的正式关卡 `assets/scenes/Watt-Station.redflag`。出生点的油桶与主角之间能看见砂石。工具台在维修棚旁，备用轮毂在路肩的草边。近景保留默认第三人称脚本、角色、战斗逻辑、HUD 和关卡光照；测试副本把玩家起点和相机距离设在可以从近处观察该物体的位置。各自的 A/B 是同一机位，动态角色的姿态仍随游戏时间变化。出生点、工具台的成组截帧在调高轮毂之前完成，路肩与轮毂的成组截帧使用新的接地位置。最终关卡另外从正式的 `--play` 入口启动并截取了默认开场。[最终关卡原生窗口](release.png)

| 机位 | 原图与同机位对照 | 观察位置 |
| --- | --- | --- |
| 出生点 | [修改前](spawn-original.png) · [只修正法线](spawn-normal.png) · [再开启视差](spawn-current.png) | 比较油桶左下方的砂石。前两张底色相同，地面受到的光发生了变化。 |
| 路肩 | [作者法线](gravel-normal.png) · [作者法线与视差](gravel-current.png) · [同区域并排](gravel-normal-current-crop.png) | 斜看石缝，视差使缝里的底色取样位置偏移。 |
| 工具台 | [原模型 1K](rack-original.png) · [重新烘焙的 2K](rack-current.png) · [架子局部](rack-original-current-crop.png) | 看层板顶面和棚柱旁绿色的漆边。 |
| 备用轮毂 | [原网格](rim-original.png) · [顶点置换](rim-current.png) · [外缘并排](rim-original-current-crop.png) | 轮孔来自原网格，锈面的起伏在近景最高也只有约半个像素。 |

## 地面、漆面和锈边

砂石网格四个角都在同一个平面里。旧实例的本地缩放是 `[100,100,1]`，法线变换把一个斜面的水平分量也缩成约百分之一。设为等比的 `[100,100,100]` 后，四个角和独立的物理地面仍在同一位置。只改缩放时，两次共同可见的 861,942 个地面像素底色完全相同，法线夹角的中位数为 5.21°。视差模式沿视线重新找取样点，沿用扫描作者给出的 RGB 法线。凹凸模式则直接从灰度图求坡度。来自同一次扫描的微表面起伏因此只计算一次。当前选择的最大高差是世界中的 2.5 厘米，换成这份模型放大前的数值为 0.00025 米。[高度图、底色和作者许可](https://polyhaven.com/a/gravel_road)

实体 1221 的工具台将原始扫描的颜色与切线空间法线烘到 2K，旧 GLB 的金属和粗糙贴图继续使用。另一架同模型的器材架仍读原图。在工具台共同可见的 243,739 个像素里，158,396 个像素的线性 RGB 距离大于 0.002。逐像素比较原图和新图，显示 RGB 平均绝对差为 5.47 个 8-bit 色阶。旧器材架仍需两张 1K 图；新增两张 2K RGBA8 贴图的完整 mip 链纹素约 42.67 MiB，驱动对齐及实际驻留峰值另计。[烘焙来源与哈希](../../../RedflagForge/assets/game/station/salvage/rack-detail/sources.json) · [作者材质](https://polyhaven.com/a/worn_metal_rack)

备用轮毂的高度图在 125–145 灰阶之间，中位数是 133。如果把白色当作原面，整圈金属会先往里缩约 12 毫米。这个实例将原面选为 0.522，Vulkan 在现有 128 字节的绘制数据中用 7 位保存这个 0 到 1 的数值。黑白高度差设为 25 毫米，作者这幅图的实际高差约为 1.96 毫米。RenderDoc 取出顶点着色器的坐标后，按原相机矩阵还原顶点移动的物理距离。按 49,320 个绘制索引计，实际移动距离中位数是 0.052 毫米，P95 是 0.345 毫米，最大为 0.821 毫米；投影到屏幕后分别为 0.035、0.195、0.511 像素。统计按索引计，同一个被三角形共用的顶点可能计入多次。原模型直径约 40.5 厘米，过去其中心放在地面，约半圈埋在地下。这份场景把这件无刚体装饰的底缘放回砂石面，置换开关比较使用相同的位置。[轮毂高度图来源](../../../RedflagForge/assets/game/station/salvage/rim-detail/sources.json) · [原始扫描模型](https://polyhaven.com/a/rusted_wheel_rim_01)

## RenderDoc 与绘制时间

设备为 NVIDIA GeForce RTX 5070 Ti Laptop GPU，驱动 616.56。Windows x64、Debug、RenderDoc 1.47、1920×1080，第 12 帧。正式游戏 EXE 的 SHA-256 为 `bc21b15c820bd4f420aee19a396c936f7da43fa4abecfad6d3500058029da87d`。原始帧保存在本机 `output/captures/local/station-materials/bc21b15c820b/`，每种配置的 JSON 记录对应帧文件、事件、真实索引数、读取的贴图尺寸、GPU 时间原始样本。四个机位的两两对照核对了目标几何的裁剪坐标矩阵。在两个机位只改变路面法向缩放时，平面的局部 Z 恒为零，比较的是实际参与顶点定位的矩阵列。

| 同一机位 | GPU 绘制原方案（微秒） | GPU 绘制新方案（微秒） | 测量范围 |
| --- | ---: | ---: | --- |
| 出生点，恢复路面法线 | 66.59 | 69.44 | 同一次路面绘制，2,048² 的底色和法线 |
| 出生点，再开启视差 | 69.44 | 141.25 | 同一张路面，另读 1,024² 高度图 |
| 路肩，再开启视差 | 126.18 | 223.33 | 同一张路面，在这个机位覆盖更多像素 |
| 工具台，提高贴图分辨率 | 49.23 | 49.02 | 旧、新、新、旧交错回放，各 10 个样本 |
| 轮毂，开启顶点置换 | 22.88 | 23.94 | 两份帧各 7 个样本，P10–P90 有重叠 |

除单独复查的工具台外，每份截帧先预热一次再测 7 次，表中给中位数。工具台首轮单独回放时，旧、新两个材质事件为 45.02 和 53.70 微秒；一帧各 draw 和 dispatch 的时间之和分别为 135 和 243 毫秒。交错复测后，整帧事件之和的中位数变成 134 和 129 毫秒。两份帧里花时最多的都是后面的两次 compute dispatch，单次耗时达数十毫秒。单个材质着色的变化不足以解释整帧波动。本次没有游戏实时帧率和 CPU 提交耗时的隔离测量。[交错复测与最慢事件](rack-timing.json) · [逐像素与顶点对照](comparison.json)

画面差值只统计两帧中都可见的目标表面。回放脚本在绘制前、绘制后以及所有后续绘制完成后读深度。一个像素本次改变了深度，且后来仍保留这份深度，才能算成画面里这件道具的一部分。两次捕获的角色动作可能略有差别。因此，两个可见范围之差表示包括前景遮挡在内的覆盖变化，轮毂本身的几何位移另用顶点着色输出核实。[GPU 回放脚本](../../../tests/replay_station_materials.py)

从仓库根目录的 PowerShell 运行以下命令。预先构建 Debug 的 Redflag 与 Forge，RenderDoc 1.47 放在 `tmp/tools/renderdoc/RenderDoc_1.47_64`，也可以通过 `--renderdoc` 指定安装位置。

```powershell
# 发起正式 --play，按相同机位截游戏第 12 帧；--time 预热后取 GPU 样本。
foreach ($view in 'spawn','gravel','rack','rim') {
    if ($view -eq 'spawn') { $variants = @('original','normal','current') }
    elseif ($view -eq 'gravel') { $variants = @('normal','current') }
    else { $variants = @('original','current') }
    foreach ($variant in $variants) {
        python tests/capture_station_materials.py $view $variant --time
    }
}
# 改成当前使用的 exe 哈希前 12 位，逐像素核对同一物体。
python tests/compare_station_materials.py tmp/station-materials/bc21b15c820b
Start-Process tmp/tools/renderdoc/RenderDoc_1.47_64/qrenderdoc.exe -ArgumentList '--python tests/replay_station_cost.py' -WindowStyle Hidden -Wait
```
