# Redflag

Redflag 是一个用 C++17 编写的 Windows 游戏引擎。用 RedflagForge 可以打开场景、摆放模型与灯光，再用 OpenGL 或 Vulkan 查看结果。

## 打开编辑器

第一次编译请看[Windows 构建说明](docs/Build-Windows.md)，里面列出了 Visual Studio 组件、Vulkan SDK、Mono 和生成工程的命令。

在 Visual Studio 中生成 Debug / x64 的 RedflagForge，然后双击 [Open-Vulkan-Editor.cmd](Open-Vulkan-Editor.cmd)。场景视口上方有“实时效果”，可以在同一机位切换 DDGI、ReSTIR、GTAO、SSR 和 TAA。直接运行编辑器时默认使用 OpenGL。[编辑器图解手册](output/pdf/Redflag-Editor-Guide.pdf) · [常用接口源码读本](output/pdf/Redflag-Source-Guide.pdf)

第一次打开 [中庭雕塑展](RedflagForge/assets/scenes/Sponza-Sculpture-Gallery.redflag)，先在仓库根目录分别运行 `python tests/fetch_sponza.py` 和 `python tests/fetch_stanford.py`。前者取得建筑和贴图，后者取得展台上的龙、兔子与佛像。回到编辑器的“项目”面板，双击场景文件就能打开布置好的展厅。[Stanford 模型与来源](output/scenes/Stanford-Scans.md) · [RHI 与 Vulkan](output/pdf/Redflag-RHI-Vulkan-Plan.pdf) · [引擎架构](output/pdf/Redflag-Architecture-LaTeX.pdf)

## 编辑器与引擎代码

要试 3D 物理，可以在 Vulkan 编辑器的项目面板打开 `Physics-Workshop.redflag`，也可以在仓库根目录运行 `Open-Vulkan-Editor.cmd --scene assets/scenes/Physics-Workshop.redflag`。点击菜单栏的“模拟物理”，红箱子会落到地台上，点“停止模拟”恢复初始位置。房间还配有两个已经重建的观察机位，可以让本机视觉模型从图里寻找物体，再由场景检查是否能拿取。[NPC 图片与操作说明](output/npc-vision/README.md) · [编辑器手册](output/pdf/Redflag-Editor-Guide.pdf)

在仓库根目录运行 `Open-Vulkan-Editor.cmd --scene assets/scenes/Character-Fox.redflag` 可以打开有皮肤和贴图的狐狸。选“狐狸 → 模型动画”，可在 Walk、Run、Survey 之间切换，暂停后拖动秒数查看姿势。模型与动作随仓提供。[示例模型与署名](RedflagForge/assets/models/reference/Fox/SOURCE.md)

同一条命令换成 `Character-Walk.redflag`，按 F5，点击视口后就能用方向键控制 BVH 角色。打开 `Character-Physics.redflag` 则可在“角色 → 物理角色（双腿）”调目标速度，观察脚落地以及停止驱动后的下落。要单独练习 FK、IK 和蒙皮，可以双击 [Open-Character-Animation.cmd](Open-Character-Animation.cmd)。[角色动画图解](output/pdf/Redflag-Character-Animation.pdf) · [实机画面与测量](output/validation/character-animation)

要观察箱子怎样从物理主机同步到另一台游戏进程，把含有 `world_server.cpp` 的 REDFServer 放在 Redflag 的同级目录。运行 `Open-World-Session.cmd server`，在另两个终端分别运行 `Open-World-Session.cmd watch`，最后在第四个终端运行 `Open-World-Session.cmd host`。两个观看者会逐行显示箱子的高度。运行 `tests\run_world_session.cmd` 还能检查漏收确认后的重传、超时和实体移除。游戏里使用的入口是 `WorldClient` 和 `NetworkScene`。[游戏示例](examples/Network/WorldWorkshop.cpp) · [常用接口源码读本](output/pdf/Redflag-Source-Guide.pdf)

用 Visual Studio 打开 `Redflag.sln`。编辑器交互在 `RedflagForge/src`，场景、资源和图形实现位于 `Redflag/src`。C# 的引擎接口放在 `Redflag-ScriptCore`，对应的游戏脚本在 `SandboxProject`。工程由根目录的 `premake5.lua` 生成。

## 从场景到画面

想调一件具体的材质，可以打开摄影棚里的古董相机。切换“镜面 IBL”，就能看见机身右边框映出的棚灯。场景使用 Khronos 的 Antique Camera 模型和 Poly Haven 的 HDR 环境图。下载命令、同机位实拍和工程里的入口见[摄影棚示例](output/profiling/material-studio/README.md)。

要试灯牌，在 Vulkan 编辑器打开 `assets/scenes/Light-Studio.redflag`。选中“灯牌”，在属性里勾选“为此物体调整材质”，即可换灯的颜色和花纹。房间里只有这块面板发光，把它移出画面，还可以观察白墙接到的暖光。[操作手册](output/pdf/Redflag-Editor-Guide.pdf) · [灯牌与神庙火盆的实际画面](output/profiling/emissive-studio/README.md)

要把房间里的间接光留给下次打开，可以在渲染设置中点“烘焙探针”，也可以从仓库根目录运行下面的 PowerShell 命令。两种方式都读取场景保存的模型、材质、点光源和“多次漫反射”设置，在场景文件旁生成 `.redflag.rfprobes`。

```powershell
cd RedflagForge
..\bin\Debug-windows-x86_64\RedflagForge\RedflagForge.exe --bake-probes assets\scenes\Light-Studio.redflag
```

第一次读项目，可以先看 Cornell Box 的两张对照图。红墙旁边的地板本来是白色，打开间接光后却带上了红色。[经典场景实测](output/pdf/Redflag-Classic-Scenes.pdf) 保存了画面和相机设置，[渲染公式与实现](output/pdf/Redflag-Rendering-Math.pdf) 顺着同一块地面解释公式、着色器和读回的颜色值。

做游戏时，可在本地浏览器打开 API 手册，按类名、方法或用途查找 C++ 与 C# 入口。想理解画面时，从《实时光照与采样》选一章，跟着教材例子读公式、看着色器，最后用同一场景的测量检查猜想。[游戏开发 API](output/api/index.html) · [实时光照与采样](output/pdf/Redflag-Realtime-Lighting.pdf) · [分章笔记与原始资料](docs/latex/lighting/README.md)

遇到错图时，可以在[排错记录](output/pdf/Redflag-Debugging-Notes.pdf)里找到对应的源码和 RenderDoc 帧。[技术选型](output/pdf/Redflag-Technical-Decisions-Updated.pdf)比较了让远处铁丝网在镜头移动时保持可见的几种取样方法。在编辑器的“渲染设置 → 质量与参数”中可以打开 SSAA 2×2。[渲染性能实测](output/pdf/Redflag-Rendering-Performance.pdf)记录了它在 Sponza 里的耗时与显存开销，也收录了深度预写、2D 批渲染和 3D 实例化的对照。各册的 LaTeX 源码与编译命令见[文档目录](docs/latex/README.md)。

日常开发使用的[项目记忆](AGENTS.md)、[AI 协作工作流](docs/AI-Workflow.md)、[图形学教材与课程](docs/Graphics-References.md)和[工程清理记录](docs/Cleanup-2026-11-21.md)也随仓库保存。

## 编译与渲染测试

安装 Visual Studio C++ 工具链，先使用 Debug / x64 构建 Redflag 和 RedflagForge，再从仓库根目录顺序运行：

```bat
tests\run_asset_tests.cmd
tests\run_shader_tests.cmd
tests\run_depth_tests.cmd
tests\run_model_tests.cmd
tests\run_render_tests.cmd
tests\run_renderer3d_tests.cmd
tests\run_ibl_tests.cmd
tests\run_scene_tests.cmd
```

GPU 测试使用隐藏的 OpenGL 4.6 上下文，检查 Shader、透明裁剪、模型导入、后处理，以及光源绘制、IBL 和场景状态。八组回归覆盖各自指定输入，不能替代完整编辑器交互或性能基准。RenderDoc 捕获与来源说明见 [截帧目录](output/captures/README.md)。

实际失败与最终通过日志的节选见 [OpenGL 回归记录](output/validation/opengl-regression.txt)。

RSM 的通量、距离衰减、采样范围与历史检查见 [RSM 专项记录](output/validation/rsm-regression.txt)，经典场景开关对照及读回数据在 [RSM 场景目录](output/scenes/rsm/)。

`tests/run_profile_tests.cmd` 生成可导入 Chrome Performance 的 CPU 时间线，输出在 `output/profiling/`。该离屏用例采用 10 轮预热和 60 轮采集，CPU 等待不等于 GPU 耗时，具体条件和实际截图见技术选型文档。

## 模型资源

模型首次加入资源库时创建同名 `.rfmeta`，记录稳定 ID。移动模型时一起移动该文件，
重新打开场景会扫描 `assets` 并恢复引用。旧场景的 `FilePath` 可继续读取，保存后增加 `ModelID`。
纹理仍沿用 `Texture2D::Create`，本轮没有引入异步导入或热重载。
