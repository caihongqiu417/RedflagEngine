# 用 Agent 检查 Redflag 场景

假设昨天保存的中庭有天空，今天看到的窗口却是一片黑。先让 agent 查清它能看到哪份场景。Redflag MCP 读取这个仓库里的场景文件，也能另开一个隐藏的 Vulkan 编辑器验证场景能否启动。你正在编辑器里拖动的灯、还没保存的相机和手上选中的物体，仍由那个窗口持有。

在仓库根目录用 PowerShell 运行下面的命令，给服务准备独立的 Python 环境。这里需要 Python 3.11 或更新版本，项目的 Vulkan 启动检查还需要生成过 Debug x64 的 RedflagForge。

```powershell
python -m venv bin-int/redflag-mcp
.\bin-int\redflag-mcp\Scripts\python.exe -m pip install -r scripts/redflag_mcp/requirements.txt
codex mcp get redflag
```

用 Codex 打开这个受信任的项目，项目配置会从该虚拟环境启动服务。开一个能加载新配置的会话，试着说：“用 `$redflag-scene` 查一下 Sponza 中庭的天空和曝光，再确认这个场景能否打开。”其他支持 stdio 的 MCP 客户端也能使用：可执行文件选这个虚拟环境的 `python.exe`，参数填本仓库 `scripts/redflag_mcp/server.py` 的完整路径。[项目连接配置](../../.codex/config.toml) · [Codex 的 MCP 接入说明](https://developers.openai.com/codex/mcp/)

| 工具 | 什么时候用 | 能看见什么 |
| --- | --- | --- |
| `get_capabilities()` | 想知道这次能不能启动引擎 | 场景目录、实际要用的编辑器是否存在，以及服务可执行的操作范围 |
| `list_scenes()` | 只记得场景叫“中庭” | 由项目目录读出的可用场景文件名 |
| `inspect_scene(scene)` | 想查哪盏灯放在走廊里，或天空是否被隐藏 | 场景的环境图、背景开关、分批返回的实体及组件 |
| `get_render_settings(scene)` | 不确定同一场景是否打开镜面 IBL、用了哪条显示曲线 | 场景旁保存的渲染设置；没有写入的选项沿用引擎默认值 |
| `check_scene(scene)` | 改完后想知道这份场景能否被真正的引擎打开 | 独立 Vulkan 进程的两帧启动检查、退出状态与日志摘录 |

比如场景配置里 `SkyVisible` 为真，启动也成功了，仍有人报告天空看不见。下一步可以用 `$redflag-frame-check` 在报告问题的机位截帧。材质图能告诉你视线是否被屋顶挡住；显示前的光照图则能帮你分辨，天空本来就是暗的，还是最后的显示设置让它变暗。随仓库的两份 skill 分别覆盖读配置和看实际画面。[场景检查 skill](../../.agents/skills/redflag-scene/SKILL.md) · [画面检查 skill](../../.agents/skills/redflag-frame-check/SKILL.md) · [中庭的实机对照](../../output/validation/atrium-sky/README.md)

这一步沿用了原 AI Native 设计里“先查能做什么，再用同一个编辑器验证”的做法。项目目前开放磁盘查询和独立启动检查。设计中提到的读取当前选区、修改未保存场景、按编辑历史撤销，需要接到编辑器主线程。[Redflag AI Native 设计](../../output/pdf/Redflag-AI-Native-Design.pdf)

从仓库根目录运行 `bin-int\redflag-mcp\Scripts\python.exe tests/check_redflag_mcp.py` 可以复查整条连接。开发机上的一次实测从启动服务、列出工具进入场景检查，独立打开 Sponza 的两帧检查耗时约 19 秒。控制台返回了打开的场景路径和实际视口尺寸。这个时间包含准备工作目录和启动编辑器，游戏帧率需要单独测量。
