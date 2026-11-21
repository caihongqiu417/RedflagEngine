# Redflag 架构文档

`main.tex` 是主文件，图由 TikZ 绘制。资源和渲染章节分别位于 `assets.tex`、`rendering.tex`。

使用 XeLaTeX（TeX Live，包含 ctex 和 TikZ）编译：

```powershell
./build.ps1
```

脚本执行两轮编译，生成 `output/pdf/Redflag-Architecture-LaTeX.pdf`。中间文件位于当前目录的 `build/`，不纳入版本管理。

字体采用 TeX Live 自带 Fandol 和 TeX Gyre，不依赖本机安装的商业字体。新增能力应先核对源码，再更新图和文字；未实现的功能只放在后续迁移章节。
