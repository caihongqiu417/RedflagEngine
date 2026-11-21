# Redflag

Redflag 是一个用 C++17 编写的 Windows 游戏引擎。用 RedflagForge 可以打开场景、摆放模型和灯光，查看 OpenGL 绘制的画面。

## 编辑器与引擎代码

用 Visual Studio 打开 `Redflag.sln`。编辑器交互在 `RedflagForge/src`，场景、资源和图形实现位于 `Redflag/src`。C# 的引擎接口放在 `Redflag-ScriptCore`，对应的游戏脚本在 `SandboxProject`。工程由根目录的 `premake5.lua` 生成。

## 从场景到画面

第一次读项目，可以先看 Cornell Box 的两张对照图。红墙旁边的地板本来是白色，打开间接光后却带上了红色。[经典场景实测](output/pdf/Redflag-Classic-Scenes.pdf) 保存了画面和相机设置，[渲染公式与实现](output/pdf/Redflag-Rendering-Math.pdf) 顺着同一块地面解释公式、着色器和读回的颜色值。

要顺着代码看一个模型怎样加载和绘制，可以接着读[引擎架构](output/pdf/Redflag-Architecture-LaTeX.pdf)。[RHI 与 Vulkan 方案](output/pdf/Redflag-RHI-Vulkan-Plan.pdf) 用一张场景颜色图说明哪些调用可以共用，OpenGL 和 Vulkan 分别怎样执行。目前编辑器仍使用 OpenGL。

遇到错图时，[排错记录](output/pdf/Redflag-Debugging-Notes.pdf) 可以从现象追到当时的源码与 RenderDoc 帧。[技术选型](output/pdf/Redflag-Technical-Decisions.pdf) 解释当时为什么选择这种实现，以及性能记录实际测到了什么。六册的 LaTeX 源码与编译命令收在[文档目录](docs/latex/README.md)。

日常开发使用的[项目记忆](AGENTS.md)、[图形学教材与课程](docs/Graphics-References.md)和[工程清理记录](docs/Cleanup-2026-11-21.md)也随仓库保存。

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
