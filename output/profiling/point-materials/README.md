# 点光源照在同一块面板上

三盏同位置、同强度的点光源照在一块橙色面板上。实验依次把材质设为非金属（粗糙度 0.25）、粗糙金属（0.70）和光滑金属（0.25）。读数取自线性 HDR，三幅图用同样的指数曝光和 sRGB 编码显示。每幅原图 33 × 33，按整数倍放大。中心像素的红通道依次为 1.16988、0.345568 和 16.1053。[非金属](dielectric.png) · [粗糙金属](rough-metal.png) · [光滑金属](smooth-metal.png)

测试用一盏灯的半径为 6 个世界单位，另外两盏分别为 4 与 3。对于能看见灯的表面，直接光乘距离平方的倒数，再乘 `max(0, 1 - d²/R²)²` 使光在半径边缘平滑降到零。非金属使用垂直入射反射率 4%，金属用底色决定高光颜色。GGX、Smith 遮蔽和 Schlick 菲涅耳项都可以在共用着色器里逐项核对。[点光源着色公式](../../../Redflag/src/Redflag/Renderer/GI/Shaders/PointLighting.glsl) · [glTF 2.0 附录 B](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#appendix-b-brdf-implementation) · [PBRT 第四版 §9.6](https://pbr-book.org/4ed/Reflection_Models/Roughness_Using_Microfacet_Theory)

在编辑器的「属性 → 点光源」设置实际的照明半径。逐灯直接光、ReSTIR DI 和分簇着色都读取此值。ReSTIR 用同一份彩色反射贡献的亮度来决定候选权重，再按被选中的概率补偿。DDGI 与这里的 ReSTIR GI 求间接漫反射，第二个表面与接收点都用 `(1-metallic)*baseColor`。这项近似忽略了漫反射能量随入射角的变化。[选择候选与绘制](../../../Redflag/src/Redflag/Renderer/GI/Shaders/GI.comp) · [探针传播](../../../Redflag/src/Redflag/Renderer/GI/Shaders/Probes.glsl) · [灯簇与阴影](../../../Redflag/src/Redflag/Renderer/ScreenSpace/Shaders/ClusterShade.comp)

Vulkan 正式场景与固定的 Sponza 漫反射基准分别核对。金属板的逐灯和分簇结果在最亮像素的红通道分别为 16.105330、16.105328。两墙漫反射试验在非金属时读到 0.27703；把照到光的地板或相机所见的后墙改成纯金属，这项降为零。固定机位的 Sponza 基准使用相同的平均底色、16 盏灯和四个随机种子，七项 MSE 复测与旧记录的最大差约为 5.1×10⁻⁹。[材质与收敛](point-materials.csv) · [墙面反射](metal-bounce.csv) · [历史基准复测](lambert-reference.json)

GPU 为 RTX 5070 Ti Laptop，Debug x64。光滑金属的 RenderDoc 1.47 捕获中，事件 8 全量逐灯，事件 22 建灯表，事件 33 分簇着色。另一个未挂 RenderDoc 的进程交替计时 21 组，之前预热四组。主线程记录整次计算调用至返回，包括等待 GPU。小面板的耗时不能外推到浴室的全尺寸窗口。[RenderDoc 原始数与摘要](renderdoc.json) · [主线程记录](point-times.csv)

从仓库根目录运行以下 Windows 命令。带 DLL 的一次会在 `output/captures/local` 写入截帧。回放使用该份捕获，未挂 RenderDoc 的一次用于主线程计时。输出 HDR 原图和 CSV 在 `tmp/screen-effects`。

```bat
set VK_ADD_LAYER_PATH=C:\Tools\RenderDoc
set VK_INSTANCE_LAYERS=VK_LAYER_RENDERDOC_Capture
tests\run_screen_effects.cmd --point-materials C:\Tools\RenderDoc\renderdoc.dll
C:\Tools\RenderDoc\qrenderdoc.exe --python tests\replay_point_materials.py
set VK_INSTANCE_LAYERS=
set VK_ADD_LAYER_PATH=
tests\run_screen_effects.cmd --point-materials
```

浴室另用正式 Vulkan 编辑器打开场景，烘焙并保存，退出，再启动第二个进程截帧。相机在门内，三盏点光源的半径都是 12，镜前灯、窗边灯、门边灯的强度分别为 6.3、4.7、2.8。RenderDoc 第 12 帧显示用原探针插值，面板为「已烘焙」。[1391 × 892 场景实拍](bathroom-pbr.png) · [属性面板](bathroom-reopened.png) · [捕获与烘焙文件的校验值](bathroom-reopen.json)

同一影响半径也用于雾的入射光。方孔前的稀雾在无限范围灯下，画面中心红通道为 0.299884；把灯限制到一个单位后，与关闭这盏灯都得到 0.244632。运行 `tests/run_fog_tests.cmd` 可复查。[不同半径的数值](fog-range.csv)
