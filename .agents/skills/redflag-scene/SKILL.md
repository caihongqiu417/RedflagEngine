---
name: redflag-scene
description: 在 Redflag 引擎项目中查场景的实体、组件、环境图和渲染设置，或用已编译的 Vulkan 编辑器复查一份已保存的场景。适用于开发者想知道“这张场景实际用了什么”和“改过后还能否打开”。
---

先通过 redflag MCP 的 `get_capabilities` 看本次能读什么。用 `list_scenes` 取得实际文件名，再把同一个名称传给 `inspect_scene` 或 `get_render_settings`。需要找某个实体时，按返回的总数翻页，保留引擎给出的字符串实体 ID。配置写在磁盘上，用户正在编辑的未保存内容由编辑器持有。

例如“关掉天空，吊灯还要反光”只涉及场景的 `SkyVisible`。查看环境图引用和渲染设置中的 `SpecularIBL`，帮助用户分别找到“显示天空背景”和“镜面 IBL”。如果用户要修改文件，先重读目标字段及附近的改动，改后再读取同一份场景核对。

要检查启动，调用 `check_scene`。它用独立的 Vulkan 编辑器进程打开场景副本，返回退出情况和诊断。像“颜色淡了”这样的视觉问题还需要取图比较。缺少服务时，项目的接入步骤在 `scripts/redflag_mcp/README.md`。
