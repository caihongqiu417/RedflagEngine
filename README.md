# Redflag
A game engine

Windows / C++17 / OpenGL 游戏引擎，编辑器为 RedflagForge。

## 工程入口

- `Redflag/src`：引擎源码与平台后端。
- `RedflagForge/src`：编辑器入口和交互层。
- `Redflag-ScriptCore`：C# 引擎 API；`SandboxProject`：游戏脚本。
- `premake5.lua`：工程生成配置；`Redflag.sln`：Visual Studio 解决方案。

## 架构与约定

- `AGENTS.md`：代码风格、注释习惯和架构方向。
- `docs/Graphics-References.md`：图形学与引擎教材、GAMES 课程、公式讲解和图片引用约定。
- `docs/Cleanup-2026-11-21.md`：本次修改、验证与尚未完成的迁移。
- [当前架构源码](docs/latex/main.tex)：LaTeX 文档，图使用 TikZ 绘制。
- `docs/latex/build.ps1`：XeLaTeX 编译脚本。
- [当前架构 PDF](output/pdf/Redflag-Architecture-LaTeX.pdf)：本轮更新的架构文档。
- [RHI 与 Vulkan 设计源码](docs/latex/rhi/main.tex)：由 `docs/latex/rhi/build.ps1` 编译。
- [RHI 与 Vulkan 设计 PDF](output/pdf/Redflag-RHI-Vulkan-Plan.pdf)：目标接口、资源同步和分阶段验收计划，Vulkan 后端尚未实现。
- [排错记录源码](docs/latex/debugging/main.tex)：由 `docs/latex/debugging/build.ps1` 编译。
- [排错记录 PDF](output/pdf/Redflag-Debugging-Notes.pdf)：错误现象、原因、修改、验证与 RenderDoc 截帧。
- [技术选型源码](docs/latex/decisions/main.tex)：由 `docs/latex/decisions/build.ps1` 编译。
- [技术选型 PDF](output/pdf/Redflag-Technical-Decisions.pdf)：候选方案、实际代价、RenderDoc 核对与讲述范围。
- [经典场景实测源码](docs/latex/scenes/main.tex)与[PDF](output/pdf/Redflag-Classic-Scenes.pdf)：Cornell Box、Sponza 的素材、设置和实际截图。
- [渲染公式源码](docs/latex/math/main.tex)与[PDF](output/pdf/Redflag-Rendering-Math.pdf)：坐标、法线、光照与深度重建，另含 RSM 通量和采样面积的推导、修改对照及 GPU 数值验证。

## 验证

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
