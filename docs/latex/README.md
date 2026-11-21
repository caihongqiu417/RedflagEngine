# Redflag 架构文档

`main.tex` 是主文件，图由 TikZ 绘制。资源和渲染章节分别位于 `assets.tex`、`rendering.tex`。

使用 XeLaTeX（TeX Live，包含 ctex 和 TikZ）编译：

```powershell
./build.ps1
```

脚本执行两轮编译，生成 `output/pdf/Redflag-Architecture-LaTeX.pdf`。中间文件位于当前目录的 `build/`，不纳入版本管理。

字体采用 TeX Live 自带 Fandol 和 TeX Gyre，不依赖本机安装的商业字体。新增能力应先核对源码，再更新图和文字。未实现的功能只放在后续迁移章节。

`rhi/main.tex` 是独立的 RHI 与 Vulkan 接入设计，描述目标接口和迁移验收，当前项目仍使用 OpenGL。运行 `./rhi/build.ps1` 生成 `output/pdf/Redflag-RHI-Vulkan-Plan.pdf`；加 `-BuildOnly` 只编译到 `rhi/build/main.pdf`。

设计图使用 TikZ。下载的官方例图及逐图来源、许可、校验值在 `rhi/figures/SOURCES.md`。

`debugging/main.tex` 维护排错与 RenderDoc 截帧，区分本轮验证、历史修复与当前限制。运行 `./debugging/build.ps1` 生成 `output/pdf/Redflag-Debugging-Notes.pdf`；加 `-BuildOnly` 只编译到 `debugging/build/main.pdf`。每条先写看到的现象，再解释原因、修改和验证。

`decisions/main.tex` 维护技术选型与取舍，说明候选方案、当前选择、成本和证据限制。运行 `./decisions/build.ps1` 生成 `output/pdf/Redflag-Technical-Decisions.pdf`；加 `-BuildOnly` 只编译到 `decisions/build/main.pdf`。没有基准测试时不写性能提升比例。两份文档共用 `rhi/preamble.tex` 的版式，各自的 `capture.tex` 维护真实截帧与观察记录。

`decisions/profiling.tex` 记录 CPU 采样条件与实际 Chrome Performance 截图。图片来自 `output/captures/` 和 `output/profiling/`，编译时应保留这些目录及原图，不能用重绘图替换真实工具截图。

`debugging/history.tex` 保存旧开发日志与 Git 记录中的排错经过，区别实际修复、指南检查项与未闭合问题。新的回归结果不能代替旧现场。

`scenes/main.tex` 用于独立记录 Cornell Box、Sponza 等经典场景的实测设置和截图，输出为 `output/pdf/Redflag-Classic-Scenes.pdf`。它与最小回归用例分开维护。

`math/main.tex` 维护渲染公式与实现推导，运行 `./math/build.ps1` 生成 `output/pdf/Redflag-Rendering-Math.pdf`。公式对应项目实际代码，并注明近似及验证范围。

`math/rsm.tex` 解释方向光 RSM 的投影通量密度、圆盘面积、几何因子和两端 Lambert 系数，包含旧提交与当前源码对照。`math/rsm-validation.tex` 记录专项 GPU 数值结果；`math/verify.py` 复算数值例并核对当前代码摘录。`debugging/rsm.tex` 只保留排错过程与证据范围，避免重复整套推导。

`debugging/review3d.tex` 记录本轮 3D、IBL 和场景状态复查，`debugging/code-cases.tex` 保存代表性错误代码与修正代码。历史片段标注提交来源，不能把当前代码改写成虚构的旧版本。
