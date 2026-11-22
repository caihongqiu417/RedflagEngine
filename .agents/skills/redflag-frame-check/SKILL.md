---
name: redflag-frame-check
description: 在 Redflag 编辑器中复查天空不见、材质发灰、曝光变化等画面问题。用真实帧区分材质贴图、材质受到的光和最终显示的颜色，并留下同机位的对照。
---

确认后端、场景、显示模式和待比较的设置。有 redflag MCP 时可从 `inspect_scene`、`get_render_settings` 读取已保存的配置。对照用同一台相机、同样的灯光和分辨率，每次只换一项，例如只关背景、保留反射，或只更换色调映射。

在仓库根目录用 `tests/capture_material_studio.py --help` 查看截帧入口。传入真实编辑器路径和目标场景，打开脚本导出的 RenderDoc 回放或场景图。若怀疑天空盖住模型，从材质图中找本帧没有画到几何体的像素；若怀疑布料褪色，分别看底色、显示转换前的浮点 RGB、最终场景图。新机位应按自己的几何取样。

把比较的两张图、变动的设置、实际使用的程序版本和观察结果交给开发者。只有启动日志时，只报告能否打开；颜色结论要对应真实图像。已有完整例子在 `output/validation/atrium-sky/README.md`。
