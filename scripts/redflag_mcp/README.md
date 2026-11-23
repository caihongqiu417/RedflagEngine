# 用 Agent 检查场景与编辑角色

制作界面图标时，可以用本机的 PNG，也可以让 agent 调用已配置的生图服务。两个工具都保存到当前游戏项目，并在图片旁记录来源。[图集、编号与生图配置](../../docs/Creator-Images.md)

假设昨天保存的中庭有天空，今天看到的窗口却是一片黑。先让 agent 查清它能看到哪份场景。Redflag MCP 读取这个仓库里的场景文件，也能另开一个隐藏的 Vulkan 编辑器验证场景能否启动。你正在编辑器里拖动的灯、还没保存的相机和手上选中的物体，仍由那个窗口持有。

## 第一次连接

在克隆的仓库根目录运行下面的命令。需要 Python 3.11 或更新版本。安装程序在当前克隆里准备依赖，随后真正连一次服务、列出工具并读取场景。

```powershell
python scripts/redflag_mcp/launch.py --setup
```

**Claude Code** 从仓库根目录运行 `claude`。首次提示时允许项目的 `redflag`，在 `/mcp` 中查看连接。终端里的 `claude mcp get redflag` 可以区分“已经找到项目配置，等待同意”和“已经连接”。开启后，让它实际调用 `get_capabilities`，核对返回的游戏目录。[Claude Code 项目配置](../../.mcp.json) · [Claude Code 官方说明](https://code.claude.com/docs/en/mcp)

**Cursor、VS Code 和 Codex** 用仓库根目录打开项目，启用项目自带的 Redflag MCP。**Claude Desktop 或其他支持本地 stdio MCP 的客户端**可以运行 `python scripts/redflag_mcp/connect.py`，把打印出的配置交给客户端。它使用当前克隆的完整路径，也适合从其他目录启动。为 Claude Desktop 自动合并配置可用 `python scripts/redflag_mcp/connect.py --install claude-desktop`，然后重启应用。若要编辑另一款游戏，生成配置时加 `--project "含有 assets 的游戏目录"`。[Cursor 配置](../../.cursor/mcp.json) · [VS Code 配置](../../.vscode/mcp.json) · [Codex 配置](../../.codex/config.toml) · [本地 MCP 客户端](https://modelcontextprotocol.io/docs/develop/connect-local-servers)

## 把一项任务交给 Agent

例如告诉它「检查维修员走到阀门后能否打开它，把动作和结果给我」。连接时服务会告诉 Agent 从哪里开始。它先用 `get_capabilities` 找当前游戏提供的试玩配置，再用 `get_workflow("playtest")` 和 `inspect_playtest_profile` 读按键、角色和预期事件。这样换到另一款游戏，也会使用那款游戏的角色和操作。

Agent 用 `begin_playtest` 开局，从 `read_playtest` 读角色位置，再用 `control_playtest` 操作。它可以用自己的模型查看 `read_playtest_image` 返回的游戏图片，决定下一步。画面附采集时的起止帧号，可以和按键回执、角色移动以及阀门打开事件对照。遇到问题，Agent 可以提出资产修改，再从同一场景开局比较。人接管操作后，Agent 应等人交还。若希望由引擎调用本机的模型连续试玩，则使用 `explore_playtest` 或 `iterate_playtest`，这两个入口还需要启动项目的模型服务。[试玩与回放](../../docs/Playtest-Harness.md)

源码、场景、角色与动画、UI 图片、试玩、修改审查和打包都有对应的 `get_workflow` 主题。每次只取当前需要的一项。工具参数由客户端直接从服务读取，主题里列出的项目 skill 也可以按名字取全文。改 C++ 或 C# 时使用客户端已有的源码编辑能力，再用 `build_project` 编译。拿返回的 `run_id` 调用 `read_build` 读退出码和日志。同一克隆里的其他客户端也能按这个编号继续查看。`run_project_check` 可以构建 NPC 或资产索引工具，或执行服务列出的固定检查。[构建依赖和首次生成工程](../../docs/Build-Windows.md)

如果客户端没列出 `redflag`，先检查它有没有读取项目配置。已经列出却连不上时，在本机运行 `python scripts/redflag_mcp/check_connection.py` 检查虚拟环境和连接。能够列出工具、读取场景，但无法开局时，再查看 `editor_exists` 和构建日志。远程聊天窗口需要客户端提供连接这台开发机的能力。本服务通过启动它的本机进程通信。

| 工具 | 什么时候用 | 能看见什么 |
| --- | --- | --- |
| `get_capabilities()` | 想知道这次能不能启动引擎 | 场景目录、实际要用的编辑器是否存在，以及服务可执行的操作范围 |
| `list_scenes()` | 只记得场景叫“中庭” | 由项目目录读出的可用场景文件名 |
| `inspect_scene(scene)` | 想查哪盏灯放在走廊里，或天空是否被隐藏 | 场景的环境图、背景开关、分批返回的实体及组件 |
| `inspect_scene_fields(scene, entity, component, proposed)` | 改角色尺寸前，先看当前的值和原设计依据 | 读保存的组件字段、单位、引擎默认值和项目取值记录，比较候选值 |
| `inspect_environment(scene)` | 想知道维修棚用哪盏灯、采石坡引用哪张图 | 保存的地形与区域、位置、地貌与植被说明、关联灯的编号及配置 |
| `set_zone_description(scene, entity, field, previous, text)` | 只把棚边植被改成旱草 | 按实体编号改一段文字。作者在此期间改过相同字段时，先读回新值 |
| `get_render_settings(scene)` | 不确定同一场景是否打开镜面 IBL、用了哪条显示曲线 | 场景旁保存的渲染设置；没有写入的选项沿用引擎默认值 |
| `set_scene_exposure(scene, original, value)` | 把当前场景的曝光从 −0.35 EV 调到 0 EV | 与 Forge 共用单字段保存。原值变化时提示冲突，实际写入后返回回读记录 |
| `check_scene(scene)` | 改完后想知道这份场景能否被真正的引擎打开 | 独立 Vulkan 进程的两帧启动检查、退出状态与日志摘录 |

比如场景配置里 `SkyVisible` 为真，启动也成功了，仍有人报告天空看不见。下一步可以用 `$redflag-frame-check` 在报告问题的机位截帧。材质图能告诉你视线是否被屋顶挡住；显示前的光照图则能帮你分辨，天空本来就是暗的，还是最后的显示设置让它变暗。随仓库的两份 skill 分别覆盖读配置和看实际画面。[场景检查 skill](../../.agents/skills/redflag-scene/SKILL.md) · [画面检查 skill](../../.agents/skills/redflag-frame-check/SKILL.md) · [中庭的实机对照](../../output/validation/atrium-sky/README.md)

场景查询和启动检查都从硬盘读取。编辑器当前的选区、没有保存的场景和编辑历史由打开的窗口管理。[Redflag AI Native 设计](../../output/pdf/Redflag-AI-Native-Design.pdf)

从仓库根目录运行 `bin-int\redflag-mcp\Scripts\python.exe tests/check_redflag_mcp.py` 可以复查整条连接。开发机上的一次实测从启动服务、列出工具进入场景检查，独立打开 Sponza 的两帧检查耗时约 19 秒。控制台返回了打开的场景路径和实际视口尺寸。这个时间包含准备工作目录和启动编辑器，游戏帧率需要单独测量。

打包时，先用 `scene_dependencies` 看已保存场景实际引用了哪些素材，再调用 `package_scene`。默认返回场景资产 ZIP 的路径和包内清单；`include_runtime=True` 会带上本机 Debug x64 的 Forge、Mono、着色器和该游戏的 C# 程序集。这个包适合在具备对应运行环境的电脑上试玩。[打包与解包启动的验证](../../tests/check_client_connection.py)

## 角色、音色和行为树

从空白建一个人，可以调用 `create_npc(new_file="guide.npc.yaml", display_name="引路人")`。返回的 `file` 是角色定义，`tree` 是它自己的行为树。新角色能够交谈，空闲时留在原地。写好人设后再加行为或选音色。在工程已经有同名角色或同名树时，本次创建会停止，手写内容会保留。

在仓库根目录运行 `scripts\redflag_mcp\build_npc_tool.cmd`。这个小程序与编辑器使用同一份角色加载器和行为树代码。先让 agent 调用 `list_npcs`，再用 `inspect_npc` 打开 `watt.npc.yaml`。瓦力有一个叫 `reserve` 的候选。收到这个键时，引擎从节点 1 走到节点 12，得到「等待队友取药」的动作。游戏中角色能不能提出这个候选，还取决于队友和库存。工具可以直接试走这条线，无须启动游戏或联系模型服务。

给 agent 的一段示例请求是：“把瓦力复制成医护员，新文件叫 `medic.npc.yaml`。保留瓦力原来的行为，把医护员的 `reserve` 连到分享物资，再只换医护员的音色。” `copy_npc` 为医护员创建独立的角色和树。查看医护员的图可以找到分享物资的编号 5。调用 `rewire_npc(npc="medic.npc.yaml", source=1, key="reserve", target=5)` 后，试跑结果会显示路径 `[1, 5]` 和动作 `share`。若把终点填成 1，引擎发现角色会回到根节点，原文件继续可用。最后用 `set_npc_voice` 写入语音服务使用的音色名。角色名称和设定文字由 `update_npc_role` 修改。音色是否能发声还需连接实际语音服务试听。[角色定义](../../Redflag/src/Redflag/AI/NpcDefinition.cpp) · [行为树](../../Redflag/src/Redflag/AI/BehaviorTree.cpp)

也可以用 Excel 或其他电子表格编辑器修改同一份角色资产。下面的命令从仓库根目录运行。先导出角色表，在表中只改目标行的「改成」列，按原格式保存为 UTF-8 制表符分隔文件，再导入。表里同时保留导出时的原值。假如同事已经改了这个音色，导入会提醒你重新导出；同事改过的其他字段可以保留。

```powershell
$python = ".\bin-int\redflag-mcp\Scripts\python.exe"
& $python scripts/redflag_mcp/npc_table.py export medic.npc.yaml profile medic.tsv
& $python scripts/redflag_mcp/npc_table.py apply medic.npc.yaml profile medic.tsv
```

把命令中的 `profile` 换成 `links` 即可编辑连线表。起点编号和候选键共同确定一条线，「改成」填新的终点编号。导入时，引擎先在临时文件中读完整棵树，所有连线都能通过检查才替换原文件。切回已经打开的行为树面板，用「重新打开」查看磁盘上的结果。运行 `bin-int\redflag-mcp\Scripts\python.exe tests/check_npc_authoring.py` 可在临时目录复查复制角色、重连、换音色、导入表格和重新打开。[表格工具](npc_table.py) · [验收脚本](../../tests/check_npc_authoring.py)

Forge 的「角色创作 → 表格」也可以导出和导入这三种表。除了角色属性和行为树连线，`intents` 用来改候选说明、目标来源和物品。以 `.csv` 结尾使用逗号，以 `.tsv` 结尾使用制表符。两种格式均使用 UTF-8。

想把落地恢复时间从约 0.21 秒改为 0.30 秒，先从 `inspect_npc("watt.npc.yaml")` 返回的 `states` 找到 `land`。这一行的 `Montage` 是相对于动作表的文件名，`Slot` 是播放它的全身层。调用 `inspect_animation_montage(montage="ai/montages/land.montage.yaml")`，在 `Events` 里找出 `Recovery` 的 `End`。将读到的旧值和新值传给 `set_montage_event_time(montage="ai/montages/land.montage.yaml", event="Recovery", field="End", original=0.20833333, seconds=0.30)`。这一操作沿用引擎加载器检查时间，文件中其余动作和作者注释保留原文。人若刚刚调过同一时刻，先重新读取再决定。独立时刻填 `At`，有始末的区间填 `Start` 或 `End`。[正式落地动作](../../RedflagForge/assets/ai/montages/land.montage.yaml)

要调持炮走路到待机的整身过渡，先用 `inspect_animation_transitions(animator="ai/watt.animator.yaml")` 查出 `walk_armed` 到 `idle` 的 `Seconds` 和 `Control`，原样交给 `set_animation_transition` 的 `original`。把新的秒数与两个控制点写入 `value`。没有自定义过渡时，目标状态的 `Blend` 决定所用秒数，引擎使用两端平滑的曲线。
## 用 MCP 审查碰撞体改动

例如想把维修站玩家的站立半径试调到 0.38 米，用 `inspect_scene_fields(scene="Watt-Station.redflag", entity="1101", component="CapsuleCollider3DComponent", proposed={"Radius": 0.38})`。返回的 `value` 取自磁盘场景，`default` 来自引擎创建新组件时的设置，`proposed` 是这次准备试的尺寸。原作者选值时的记录放在 `tuning` 中，改过尺寸后仍可以查。把组件换为 `ScriptComponent` 可以查看场景中已保存的脚本字段，当前支持预览其中的 Float 和 Vector3。[引擎字段定义](../../Redflag/src/Redflag/Scene/ComponentFields.json) · [维修站的取值记录](../../RedflagForge/assets/scenes/Watt-Station.tuning.yaml)

读完后，把返回的 `operations` 和这次修改的意图交给 `propose_asset_change`。每项可以另填 `reason` 说明这次改值的理由，用 `evidence` 列出测试文件。保留返回的修改编号，在 Forge 的创作审查中核对，或用 `apply_asset_change(change=编号)` 保存。有其他人改过同一项时，工具会显示新的值。用 `undo=True` 可以撤销。游戏运行或暂停时都有自己的场景副本，重新从已保存的场景开局即可测试新尺寸。[字段修改与撤销](asset_changes.py) · [使用临时项目的验收脚本](../../tests/check_component_authoring.py)

## 项目的界面图片

先用 `list_ui_layouts` 找到当前游戏的布局，用 `inspect_ui_layout` 看哪些文字和图片正在使用。假设维修站的工单图叫 `ticket`，想换成另一张 PNG，就把布局路径、`ticket`、刚读到的原文件名和新图在项目里的路径交给 `set_ui_sprite`。已有布局中的字号、文字间距和其他图片都能保留。重新打开游戏就可以在真实画面上检查。换一个游戏时，用 `REDFLAG_PROJECT` 指向那个项目的根目录，仍用这一组工具。[图片导入与生成](../../docs/Creator-Images.md)

在 Forge 中审查工具产生的改动、比较两局试玩、从帧号回到原始截图，见[创作审查使用说明](../../docs/Creation-Review.md)。

用自己的游戏接入自动试玩，或复现操作员走不动、模型修订并重新开局的整段过程，见[游戏试玩与修订](../../docs/Playtest-Harness.md)。
