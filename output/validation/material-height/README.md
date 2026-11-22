# 碎石表面的高度

用 Vulkan Forge 打开 `assets/scenes/Relief-Study.redflag`。层级中选“砂石”，在“模型绘制”中展开“部件 1 的材质”。“高度贴图效果”可以在“关闭”“凹凸”“视差遮蔽”“顶点置换”之间切换。示例已经选好了贴图。给别的模型使用时，先勾“为此物体调整材质”，再展开部件并点“选择高度贴图”。调节“黑白高度差（模型米）”可以控制起伏的大小，白色位于模型原表面。

示例用一块边长两米的规则网格，每边 64 段。石子底色、粗糙度和高度来自 Amal Kumar 的 CC0 扫描，原作者记录的拍摄宽度也是两米。新加入的原图是 1024×1024 的灰度高度图。示例把最高到最低的距离设为 5.5 厘米，便于比较轮廓；用户可按自己的场景调整。四张对照图均来自正式 Vulkan 光照代码（单采样、固定机位、关闭 TAA）。[Poly Haven 原始材质与许可](https://polyhaven.com/a/gravel_road)

| 方式 | 画面 | 底色变化 | 法线变化 | 深度变化 | 拾取编号变化 |
| --- | --- | ---: | ---: | ---: | ---: |
| 关闭 | [原图](off.png) | 0 | 0 | 0 | 0 |
| 凹凸 | [原图](bump.png) | 0 | 187854 | 0 | 0 |
| 视差遮蔽 | [原图](parallax.png) | 183935 | 187825 | 0 | 0 |
| 顶点置换 | [原图](displacement.png) | 184223 | 187767 | 188020 | 6018 |

表中是在 768×432 像素内，和关闭高度时相比有变化的位置数。逐像素比较线性底色的 RGB 距离（阈值 0.002）、单位法线的三维距离（0.01）、存储深度的绝对差（0.00001）和整数拾取编号（精确比较）。正交相机从正上方看时，启用视差也不会移动底色。把高度改为 0 时，四项与原材质逐像素相同。模型单独用 OBJ 输入覆盖了灰度 `map_Bump` 和点名 `_normal` 的旧 RGB 法线图；另外实测了材质复制及场景保存后重新打开。[逐项结果](metrics.csv) · [游戏窗口实拍，1920×1080](game.png)

## 绘制用时

在 NVIDIA GeForce RTX 5070 Ti Laptop GPU 上采样，Windows、x64 Debug。同一网格在同一视口内画一次，四组均为 RenderDoc 事件 11、24,576 个索引和一个实例。每次回放先预热一次，再测六次；依次测四种模式，再按相反顺序重复，共 12 个 GPU 样本。这里计的是写入底色、法线和深度的绘制事件，未计入后处理、CPU/GPU 同步或整个游戏帧。CPU 单独测场景收集到最后一条绘制命令写完，提交队列在计时段外。按同一轮换顺序，每轮预热一帧再采 16 帧，每种模式共 32 帧。

| 模式 | CPU 录制中位数（µs） | CPU P10–P90（µs） | GPU 绘制中位数（µs） | GPU P10–P90（µs） |
| --- | ---: | ---: | ---: | ---: |
| 关闭 | 40.15 | 37.74–75.59 | 17.712 | 17.54–17.92 |
| 凹凸 | 40.05 | 38.22–47.64 | 18.720 | 18.63–19.96 |
| 视差遮蔽 | 40.00 | 37.81–72.45 | 45.376 | 44.22–45.76 |
| 顶点置换 | 39.70 | 37.91–77.04 | 19.040 | 18.85–19.23 |

四个模式的录制时间接近。斜看碎石时，视差模式沿每个像素的视线多次读高度图，GPU 耗时最高。置换读取 4,225 个网格顶点的高度；换成更密的模型，须重新测量。三种模式当前用于 Vulkan 的不透明材质。旧 OpenGL 路径保留原渲染。游戏物理、离线或静态 GI 中用于射线相交的三角形仍来自源模型。RenderDoc 回放的四组均无中、高等级消息。全帧率和队列提交的耗时尚未采样。[全部 CPU 样本](record-cpu-us.csv) · [四份本机 RDC 的路径、SHA-256、事件与 GPU 原始样本](replay.json)

## 纹理存储

原图 PNG 占 1,740,327 字节。读进显卡时，当前 Vulkan 材质把它解码为每像素四个字节的 RGBA8，高度只读取红色。把 1024² 到 1² 的所有 mip 层加起来，纹素本身的大小为 5,592,404 字节。若改用单通道的 BC4，每个 4×4 小块占 8 字节，最小的一层也占一整块，按格式计算共 699,064 字节。两个数都没有计入驱动分配的对齐和额外开销。

BC4 仍属下一轮实验。应先核对目标设备对该格式的采样和线性过滤，再用同一相机测轮廓、法线与视差的误差，并实测解码、上传时间与显存分配。在有陡坡的高度图上，块内量化造成的小误差也可能改变石面的亮暗。单通道无压缩 R8 是可直接比较的第三种输入。项目的视差实现参考 LearnOpenGL，未找到能核实《红色沙漠》使用相同着色器或高度压缩格式的一手出处。[Khronos 的格式说明](https://docs.vulkan.org/spec/latest/chapters/formats.html) · [LearnOpenGL 的视差遮蔽推导](https://learnopengl.com/Advanced-Lighting/Parallax-Mapping)

## 复现

从仓库根目录运行，预先构建 Debug 引擎和 Forge。游戏窗口使用 `RedflagForge` 作为工作目录。2026-11-23 实拍所用 Forge SHA-256 为 `7a0302c2b2e827c47ce2fcd557ea38dd70b979acf2fda1e714ce13723cf5825b`。

```powershell
# 回归会保存四张光照图、逐像素结果以及 CPU 样本。
tests/run_scene_tests.cmd --height-material
tests/run_rhi_tests.cmd

# 打包示例应包含应用的高度图、两米网格和所引用的背景与材质。
python scripts/asset_bundle.py RedflagForge/assets/scenes/Relief-Study.redflag tmp/height-study.zip

# RenderDoc 1.47。路径改为本机安装目录。
$rd = (Resolve-Path 'tmp/tools/renderdoc/RenderDoc_1.47_64').Path
$env:VK_ADD_LAYER_PATH = $rd
$env:VK_INSTANCE_LAYERS = 'VK_LAYER_RENDERDOC_Capture'
0..3 | ForEach-Object { tests/run_scene_tests.cmd --height-material $_ "$rd/renderdoc.dll" }
Remove-Item Env:VK_INSTANCE_LAYERS
Remove-Item Env:VK_ADD_LAYER_PATH
Start-Process "$rd/qrenderdoc.exe" -ArgumentList '--python tests/replay_material_height.py' -WindowStyle Hidden -Wait
```

Vulkan 顶点置换的入口为 `Redflag/src/Platform/Vulkan/Shaders/Mesh.vert::main`；坡度和视差交点见同目录 `HeightSurface.glsl::bumpNormal` 和 `reliefUV`。CPU 将游戏材质交给后端的位置是 `SceneRenderer::DrawGeometry`。PDF 中的“碎石表面的高度”一节给出了同机位图、简单推导和绘制时间。
