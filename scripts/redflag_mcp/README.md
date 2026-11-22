# 用 Agent 检查场景与编辑角色

制作界面图标时，可以用本机的 PNG，也可以让 agent 调用已配置的生图服务。两个工具都保存到当前游戏项目，并在图片旁记录来源。[图集、编号与生图配置](../../docs/Creator-Images.md)

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
| `inspect_environment(scene)` | 想知道维修棚用哪盏灯、采石坡引用哪张图 | 保存的地形与区域、位置、地貌与植被说明、关联灯的编号及配置 |
| `set_zone_description(scene, entity, field, previous, text)` | 只把棚边植被改成旱草 | 按实体编号改一段文字。作者在此期间改过相同字段时，先读回新值 |
| `get_render_settings(scene)` | 不确定同一场景是否打开镜面 IBL、用了哪条显示曲线 | 场景旁保存的渲染设置；没有写入的选项沿用引擎默认值 |
| `check_scene(scene)` | 改完后想知道这份场景能否被真正的引擎打开 | 独立 Vulkan 进程的两帧启动检查、退出状态与日志摘录 |

比如场景配置里 `SkyVisible` 为真，启动也成功了，仍有人报告天空看不见。下一步可以用 `$redflag-frame-check` 在报告问题的机位截帧。材质图能告诉你视线是否被屋顶挡住；显示前的光照图则能帮你分辨，天空本来就是暗的，还是最后的显示设置让它变暗。随仓库的两份 skill 分别覆盖读配置和看实际画面。[场景检查 skill](../../.agents/skills/redflag-scene/SKILL.md) · [画面检查 skill](../../.agents/skills/redflag-frame-check/SKILL.md) · [中庭的实机对照](../../output/validation/atrium-sky/README.md)

场景查询和启动检查都从硬盘读取。编辑器当前的选区、没有保存的场景和编辑历史由打开的窗口管理。[Redflag AI Native 设计](../../output/pdf/Redflag-AI-Native-Design.pdf)

从仓库根目录运行 `bin-int\redflag-mcp\Scripts\python.exe tests/check_redflag_mcp.py` 可以复查整条连接。开发机上的一次实测从启动服务、列出工具进入场景检查，独立打开 Sponza 的两帧检查耗时约 19 秒。控制台返回了打开的场景路径和实际视口尺寸。这个时间包含准备工作目录和启动编辑器，游戏帧率需要单独测量。

## 角色、音色和行为树

在仓库根目录运行 `scripts\redflag_mcp\build_npc_tool.cmd`。这个小程序与编辑器使用同一份角色加载器和行为树代码。先让 agent 调用 `list_npcs`，再用 `inspect_npc` 打开 `watt.npc.yaml`。瓦特有一个叫 `reserve` 的候选。收到这个键时，引擎从节点 1 走到节点 12，得到「等待队友取药」的动作。游戏中角色能不能提出这个候选，还取决于队友和库存。工具可以直接试走这条线，无须启动游戏或联系模型服务。

给 agent 的一段示例请求是：“把瓦特复制成医护员，新文件叫 `medic.npc.yaml`。保留瓦特原来的行为，把医护员的 `reserve` 连到分享物资，再只换医护员的音色。” `copy_npc` 为医护员创建独立的角色和树。查看医护员的图可以找到分享物资的编号 5。调用 `rewire_npc(npc="medic.npc.yaml", source=1, key="reserve", target=5)` 后，试跑结果会显示路径 `[1, 5]` 和动作 `share`。若把终点填成 1，引擎发现角色会回到根节点，原文件继续可用。最后用 `set_npc_voice` 写入语音服务使用的音色名。角色名称和设定文字由 `update_npc_role` 修改。音色是否能发声还需连接实际语音服务试听。[角色定义](../../Redflag/src/Redflag/AI/NpcDefinition.cpp) · [行为树](../../Redflag/src/Redflag/AI/BehaviorTree.cpp)

也可以用 Excel 或其他电子表格编辑器修改同一份角色资产。下面的命令从仓库根目录运行。先导出角色表，在表中只改目标行的「改成」列，按原格式保存为 UTF-8 制表符分隔文件，再导入。表里同时保留导出时的原值。假如同事已经改了这个音色，导入会提醒你重新导出；同事改过的其他字段可以保留。

```powershell
$python = ".\bin-int\redflag-mcp\Scripts\python.exe"
& $python scripts/redflag_mcp/npc_table.py export medic.npc.yaml profile medic.tsv
& $python scripts/redflag_mcp/npc_table.py apply medic.npc.yaml profile medic.tsv
```

把命令中的 `profile` 换成 `links` 即可编辑连线表。起点编号和候选键共同确定一条线，「改成」填新的终点编号。导入时，引擎先在临时文件中读完整棵树，所有连线都能通过检查才替换原文件。切回已经打开的行为树面板，用「重新打开」查看磁盘上的结果。运行 `bin-int\redflag-mcp\Scripts\python.exe tests/check_npc_authoring.py` 可在临时目录复查复制角色、重连、换音色、导入表格和重新打开。[表格工具](npc_table.py) · [验收脚本](../../tests/check_npc_authoring.py)

Forge 的「角色创作 → 表格」也可以导出和导入这三种表。除了角色属性和行为树连线，`intents` 用来改候选说明、目标来源和物品。以 `.csv` 结尾使用逗号，以 `.tsv` 结尾使用制表符。两种格式均使用 UTF-8。

在「动画」页找到 `pickup`，可以从当前模型的剪辑里挑选拾取动作。手碰到医疗包的时刻用动作长度的比例表示。如果一段动作指定为 0.9 秒、接触比例是 0.58，游戏在约 0.52 秒时完成交互。MCP 的 `inspect_npc` 会列出同一份动作表。`set_npc_animation(npc="medic.npc.yaml", state="pickup", contact=0.58)` 只修改这一项。输入 1.2 时引擎会拒绝保存，原来的时间仍可使用。[动画状态的读取与校验](../../Redflag/src/Redflag/Animation/AnimationController.cpp)
## 项目的界面图片

先用 `list_ui_layouts` 找到当前游戏的布局，用 `inspect_ui_layout` 看哪些文字和图片正在使用。假设维修站的工单图叫 `ticket`，想换成另一张 PNG，就把布局路径、`ticket`、刚读到的原文件名和新图在项目里的路径交给 `set_ui_sprite`。已有布局中的字号、文字间距和其他图片都能保留。重新打开游戏就可以在真实画面上检查。换一个游戏时，用 `REDFLAG_PROJECT` 指向那个项目的根目录，仍用这一组工具。[图片导入与生成](../../docs/Creator-Images.md)

在 Forge 中审查工具产生的改动、比较两局试玩、从帧号回到原始截图，见[创作审查使用说明](../../docs/Creation-Review.md)。

用自己的游戏接入自动试玩，或复现操作员走不动、模型修订并重新开局的整段过程，见[游戏试玩与修订](../../docs/Playtest-Harness.md)。
