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
- `docs/Graphics-References.md`：图形学教材、公式讲解和图片引用约定。
- `docs/Cleanup-2026-11-21.md`：本次修改、验证与尚未完成的迁移。
- `docs/latex/main.tex`：LaTeX 架构文档源码，图使用 TikZ 绘制。
- `docs/latex/build.ps1`：XeLaTeX 编译脚本。
- `output/pdf/Redflag-Architecture-LaTeX.pdf`：本轮更新的架构文档。

## 验证

在安装了 Visual Studio C++ 工具链的环境运行 `tests/run_asset_tests.cmd`。
完整编辑器使用 Visual Studio 的 Debug / x64 配置构建 RedflagForge。
构建后运行 `tests/run_render_tests.cmd`，在隐藏 OpenGL 4.6 窗口中验证后处理。

## 模型资源

模型首次加入资源库时创建同名 `.rfmeta`，记录稳定 ID。移动模型时一起移动该文件，
重新打开场景会扫描 `assets` 并恢复引用。旧场景的 `FilePath` 可继续读取，保存后增加 `ModelID`。
纹理仍沿用 `Texture2D::Create`，本轮没有引入异步导入或热重载。
