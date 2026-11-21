# Redflag 的 AI 协作工作流

## 做一次渲染优化

从改动要节省的工作开始。2D 合批想减少重复提交，先固定同一组精灵，逐个画一遍，再整批画一遍。深度预写想少给被遮挡的表面着色，就要同时检查预写新增的工作和主绘制省下的工作。先读实际调用代码，再选能观察到这项差别的场景。

截帧时保留两种画法的输入，在 RenderDoc 找到对应事件，读出索引数、实例数和可用的硬件计数。对照同一张图的颜色、深度或拾取编号，确认画面的一致性。耗时另开进程采集。保留每一帧的 CPU 和 GPU 记录，LaTeX 性能册编译时从 CSV 生成统计表。原始帧、命令与本机条件随实验一起保存。[2D 精灵实验](../output/profiling/batch2d/README.md) · [3D 网格实验](../output/profiling/mesh/README.md) · [渲染性能实测](../output/pdf/Redflag-Rendering-Performance.pdf)

本次用 RenderDoc 检查 2D 合批时，事件面板里清空拾取图的调用出现了意外的整数。查看测试才发现只传入一个整数，而 OpenGL 要求传入四个分量。拾取图只存第一分量，所以最终画面对比仍然通过。修正后，重新采集了捕获、事件截图和计时数据。为避免以后换了帧却沿用旧报告，性能册在编译时先核对四份捕获的 SHA-256，再从每帧计时重新生成表格。[Khronos 清屏接口原文](https://github.com/KhronosGroup/OpenGL-Refpages/blob/main/gl4/glClearBuffer.xml)

## 查资料与接入工具

已有工具能回答的问题直接用现成入口。本次用开源的 renderdoc-mcp v0.3.0 打开原始帧、列出绘制事件，再用 RenderDoc 1.47 的回放 API 检查附件和硬件计数，事件面板截图保留选中的实际调用。引擎的 CPU 埋点和 OpenGL 的时间查询回答不同问题，计时时不加载截帧工具。[renderdoc-mcp 发布记录](https://github.com/JiaboLi-GitHub/renderdoc-mcp/releases/tag/v0.3.0) · [RenderDoc 事件面板](https://github.com/baldurk/renderdoc/blob/v1.x/docs/window/event_browser.rst) · [Khronos 计时查询](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_timer_query.txt)

遇到缺少的能力，可以查找和下载合适的开源工具或技能。先看上游仓库、许可、版本和它实际需要的访问范围，拿一个已有结果的项目样例试用。采用时在这里补上触发条件、版本、可复现入口和它补足的能力；与现有工具重复或在本项目中结果不可靠，也留下简短理由。设计和公式引用的章节仍归入[图形学资料索引](Graphics-References.md)。

## 什么时候同步给用户

新工具在项目样例上验证可用时，说明它替代了哪项手工工作。排错改变了原来的判断时，给出事件号或像素读数。优化结果持平、变慢，或只对某一类场景有效时，说明测试条件和下一次怎样验证。最后把源码、原始数据、编译后的文档放在同一份交付里，让下一次工作可以从已有证据接着做。
