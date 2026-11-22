# 镜头外的灯牌

白墙位于 `z=0`，漫反射率为 0.8。与墙平行的灯牌在 `z=1`，水平方向覆盖 `[0.4,1.6]`，高 1 个场景单位。17 × 17 的正交画面只覆盖墙中央宽 0.5 的部分。模型自身的底色为黑，使用第一组 UV；发光贴图使用第二组 UV，左边是红色 255、右边是绿色 128。线性发光因子为 `(1,0.5,0)`，强度为 4。没有点光源、环境图、Bloom 和曝光处理。所有数值都是色调映射前的线性 HDR。该模型和数据由项目自行制作。[glTF 夹具](../../../tests/fixtures/EmissivePanel.gltf) · [CPU 面积积分与 GPU 检查](../../../tests/EmissiveGICheck.h)

CPU 用 128 × 96 个小方块对整块灯牌做面积积分，两个纹素先解码为线性值再插值。GPU 则从白墙往场景发出按余弦加权的射线。墙面参考平均 RGB 为 `(0.215215,0.0140605,0)`；每像素 2,048 条射线得到 `(0.215356,0.0141098,0)`。ReSTIR GI 每像素每帧一个新候选，512 帧平均为 `(0.213964,0.0149291,0)`。这两种算法会随采样方向而起伏，DDGI 还会用邻近探针插值。第 24 帧把每个方向的射线从 3 增至 16，邻接像素红色差的误差从 0.01024 降到 0.00475，但平均红色同时从 0.21282 变成了 0.17053。应一起看亮度与平滑程度。[接入自发光时的 CSV](measurements.csv) · [glTF 2.0 §5.19.7](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#material-emissivetexture) · [发光强度扩展](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_emissive_strength) · [PBRT 第四版 §12.4](https://pbr-book.org/4ed/Light_Sources/Area_Lights)

烘焙值是 `(0.199810,0.010965,0)`。第二个独立进程从磁盘加载探针后，289 个像素的 4 个浮点分量全部与第一次一致。改变发光因子、贴图或灯牌朝向会拒绝旧探针。在随后修改射线求交之前，我们还专门验证了只接入自发光的影响。用接入前的源码烘焙两只不发光的盒子，再用刚接入自发光的版本读回并重新烘焙，两个探针文件的 SHA-256 都是 `063a57ad7d33d5464290b5de44283fdaaebc151ce5d82e3a2ca91aad88386b54`。场景使用原点处的红盒与 x=1.3 处的白盒、位置 (0,2,0) 的点光源、强度 (8,4,2) 和四轮反馈，其余参数用默认值。

环境是 Windows、Debug x64、NVIDIA GeForce RTX 5070 Ti Laptop GPU，打开 Vulkan 验证。RenderDoc 1.47 在第 512 次 ReSTIR GI 的事件 8 记录候选，事件 20 记录复用。两次都提交 5 组工作，回放结果的蓝色全为零，捕获中的诊断消息为空。本机的 renderdoc-mcp 0.3.0 所带回放库无法打开这份 Vulkan 格式 32 的捕获，事件和像素采用产生捕获的 1.47 官方回放读取。[回放结果、原帧路径和 SHA-256](renderdoc.json) · [回放脚本](../../../tests/replay_emissive_gi.py)

从仓库根目录运行，先构建 Debug/x64 的 RedflagForge。前两条命令各启动一次测试。截帧时将下列 RenderDoc 目录替换为实际安装目录；回放前清除注入捕获层的环境变量。

```bat
tests\run_gi_benchmark.cmd --emission
tests\run_gi_benchmark.cmd --load-emission
set "VK_ADD_LAYER_PATH=C:\Tools\RenderDoc"
set "VK_INSTANCE_LAYERS=VK_LAYER_RENDERDOC_Capture"
tests\run_gi_benchmark.cmd --emission "%VK_ADD_LAYER_PATH%\renderdoc.dll"
set VK_INSTANCE_LAYERS=
set VK_ADD_LAYER_PATH=
C:\Tools\RenderDoc\qrenderdoc.exe --python tests\replay_emissive_gi.py
```

## 射线落在墙的接缝上

上面的灯牌面积积分将相机水平移了 0.007 个场景单位。另一次将相机移回正中央，17 × 17 个像素都应首先碰到白墙的正面（世界坐标 z = 0）。旧版有 10 条射线沿公共边漏到了背面（z = −0.1），中心像素 (8, 8) 就是其中之一。判定阈值是 0.0001 个场景单位，墙厚为该阈值的 1,000 倍。[旧版像素读数](edge-current.csv) · [旧版交点图，红格表示背面](edge-current.png)

先前另做的投影试验得到 289 个正确交点。正式版本随后保留每个三角形的三个顶点，将它们沿射线方向变换到同一平面，用三条有向边检查交点。现在同一机位的 289 条射线全部命中正面，中心像素的 z 为 0，整幅图中最大的正面位置误差是 0.000000477 个场景单位。真实留宽 0.002 的缝隙仍能让中心射线穿过。另将八个三角形拼为一面墙，沿三个轴从两侧以正交和透视相机分别观察，12 个视角的 3,468 条射线全部命中正确的平面。[早期独立试验](edge-projected-trial.csv) · [正式版本逐像素坐标](edge-final.csv) · [12 个视角](edge-directions.csv)

从仓库根目录运行 `tests\run_gi_benchmark.cmd --triangle-edge`。同一命令还会用点光源照白墙，分别把小挡板放在灯前、灯后以及换成透光的薄玻璃。中心像素的线性红通道依次为 0、0.168192、0.168192。这组数据来自正式 Debug x64、Vulkan、RTX 5070 Ti Laptop GPU。着色器和三角形数据的校验值参与探针缓存签名，使用新求交的场景要重新烘焙。[测试条件与着色器摘要](edge-measurement.json) · [PBRT 第四版 §6.5.3](https://pbr-book.org/4ed/Shapes/Triangle_Meshes#TriangleIntersection) · [Woop、Benthin、Wald，JCGT 2013](https://jcgt.org/published/0002/01/05/paper.pdf)

另用一个三角形从 x＝−8192 延伸到 x＝0.00025，在同一条边接上一个小三角形。镜头放大接缝附近 0.001 个场景单位，17 × 3 条射线全部命中墙面。使用最终版的求交与建树方式，再按文首条件重测灯牌，2,048 个样本的平均红色仍为 0.215356，512 帧 ReSTIR GI 为 0.213967，DDGI 烘焙为 0.199809。同一份烘焙关掉程序后重新打开，289 个像素逐个相同。[最终版本的灯牌读数](measurements-watertight.csv) · [长三角形模型](../../../tests/fixtures/LongSharedEdge.obj)
