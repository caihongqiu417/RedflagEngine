# 潮汐维修站

进入维修站后，你操控蓝灰色机器人，左前方的赭色机器人叫瓦力。按 **T**，输入「你叫什么名字？」，再按 **Enter**。输入框会收起，你可以继续走动，听瓦力回答。它说的话也会出现在画面下方。想用麦克风，就在输入框旁用鼠标按住「按住说话」，说完松开。按 **Esc** 可以收起还没发出的草稿。[玩家与创作手册](../output/pdf/Redflag-Watt-Guide.pdf)

瓦力也能帮你做事。再按 **T** 说「把你背包里的一份医疗包送给我」，看它靠近并伸手递药。交接前你们各有一份，交接后左下角会显示你有两份。听到它答应时，药还在它手里。[发布包递药实测](../output/validation/packaged-watt/release-delivery/result.json)

## 开始游戏

用 **WASD** 移动，按住 **Shift** 跑，按住 **Ctrl** 蹲着走。按 **F** 用左手出拳，收拳后可以再按。**鼠标左键** 使用右臂选中的工具，**1** 选臂炮，**2** 选扫描器。对准可拾取物品按 **E**，机器人会先伸手，够到医疗包才收进背包。受伤时按 **H**，左手把药盒举到胸前，一份药最多恢复 50 点生命。走近瓦力按 **G** 可以把自己的一份药递给它。想让受伤的瓦力自己用药，按 **T** 告诉它「用你背包里的药修复你自己」。

交付目录为 `output/releases`。本机已经解压好，打开 `Watt-Station-Win64`，双击 `Play-With-Companion.cmd`。换一台电脑时，把 `Redflag-Watt-Station-Win64.zip` 与 `Redflag-CompanionRuntime-Win64.zip` 解压到同一个文件夹。启动脚本旁应当能看见 `CompanionRuntime`。需要 Windows x64、支持 Vulkan 1.3 的显卡驱动和足够的显存。瓦力在递药、维修和救援后会用随包录好的 Dylan 台词回应。本机已配置 Delivery 语音时，新回答沿用 Dylan。单独使用便携包时，可以用 Windows 安装的简体中文音色朗读；没有可用语音仍能读字幕。第一次启动时，程序需要把本地模型载入内存。[离线启动、看图和中文语音实测](../output/validation/packaged-companion/README.md)

维修棚里的应急发电机还没通电。你可以过去按住 **E** 维修，也可以告诉瓦力「去东面的应急发电机，把它修好」，自己先去西侧闸口。等机器供电、两人都到闸口，任务才会结算。按住 **F1** 查看键位，结束后按 **R** 重新开局。新版货场记录了瓦力独自修好发电机、受伤返回并和玩家一起撤离的整局过程，也验证了任务失败后可以重新开始。[货场试玩记录](../output/validation/packaged-watt/freight-yard/README.md)

游戏默认 1920 × 1080。如果转动镜头时有明显迟滞，退出后双击 `Play-720p.cmd`，它会用 1280 × 720 绘制场景，保留原来的窗口、界面字号和队友通讯。在 RTX 5070 Ti Laptop 上固定出生机位，两种模式的帧时间中位数分别为 87.77 和 58.12 毫秒。720p 转动起来更快，目前两档都达不到稳定的 30 帧。[帧时间实测](../output/validation/packaged-watt/resolution.json)

## 穿过货场

出生点旁是补给区。向东能看见行车和远处的铁皮棚，发电机在棚下。中央车道比较短，但要留意间歇带电的地面和巡检机。绕到北侧，货柜可以挡住观察窗的视线。队友可以带着药随行，也可以先去维修。供电恢复后，电缆停止放电，西侧闸口亮灯。

巡检机会记住维修员刚刚出现的位置，离开视野后只到那里查看。再次靠近时看机身的橙色蓄能光。完整的一轮需要 0.8 秒，途中目标躲开就要重新蓄能。在 Forge 层级中选择「故障巡检机」，可以看行为树正在走哪条分支，脚本属性可调感知距离、蓄能时间和脉冲伤害。展开黑板可以看到这次判断用的目标、视线和蓄能进度。[巡检机行为树](../RedflagForge/assets/ai/sentinel-patrol.tree.yaml) · [两条路线实机记录](../output/validation/station-encounter/routes/README.md)

《三角洲行动》的 GDC 讲座简介介绍了用关卡工具和游戏数据复查设计。Team Jade 在「纪念碑」访谈里还讲了从城镇、沙丘到建筑内部的推进感。维修站沿用自己的货柜、设备与维修任务，用路线用时、巡检机反应和队友能否撤离来检查这次布局。[GDC 2025 讲座简介](https://gdcvault.com/play/1035407/Tencent-Games-Developer-Summit-Large) · [Team Jade 地图设计访谈](https://news.xbox.com/en-us/2025/11/19/making-of-delta-forces-newest-map-monument/)

## 在 Forge 看一轮对话

在 Forge 打开维修站，按 **F6** 播放，点「主相机 Game」，在画面上按 **T** 问「你叫什么名字？」。听到回答后，在层级里选中瓦力，在属性面板的「AI 角色」里点「本局请求与动作」。在「请求记录」的下拉列表选「角色答话」，打开「模型」页。「推理输入 JSON」里是这轮实际送给模型的消息，「模型生成的原文」里是它的回答。

检查发行包时，`tests/check_station_package.py --conversation` 会把这两步的记录存到 `--output` 指定目录下的 `dialogue-trace.jsonl`。顺着同一个角色看，「对话意图」的 `choice` 表示游戏选了闲聊、回忆还是观察现场，「角色答话」的 `model_input` 和 `model_output` 对应 Forge 里看到的模型消息和生成原文。用 `seq` 可以在同目录的 `frames.jsonl` 找到对白出现时的游戏帧。普通玩家启动游戏时默认关闭这项记录。

回到游戏里说「把药送到我手上」，再到请求记录中选「子树行动选择」。展开「本次提供的选项」，看看这次有没有送药、留药，哪一项旁边标了「已选」。切到「动作库」，就能对照「分享物资」这个行为树节点和它执行的 `share`。等瓦力递出药，再看游戏左下角两人的数量是否改变。[维修站的行为树](../RedflagForge/assets/ai/watt.tree.yaml)

## 修改队友

在源码目录运行 `Open-Vulkan-Editor.cmd --scene assets/scenes/Watt-Station.redflag`。点「AI → 角色资产…」。要从头建一个人，点「新建角色…」，角色名称填「引路人」，资产文件填 `guide.npc.yaml`，点「创建」。新角色会原地等候，也能陪人说话。在「角色」页写下它的身份、说话习惯及观察设置。点「行为树 → 打开行为树编辑器」可以查看等候动作。如果它要用在关卡里，停止播放，在层级里选中带「AI 角色」组件的实体，点「绑定当前角色资产」并保存场景。[瓦力的角色文件](../RedflagForge/assets/ai/watt.npc.yaml) · [行为树](../RedflagForge/assets/ai/watt.tree.yaml) · [C# 维修动作](../examples/npc-runtime/RepairOrder.cs) · [引擎 API](../output/api/index.html)

已有的角色可以从「项目资产」再打开，也能从实体的「AI 角色 → 编辑角色资产」直接进入。要给整个项目换模型，打开「AI → 模型与语音设置…」，选择本机或外部模型。外部服务需要填写聊天地址和模型名。此处也可以指定本机 Delivery 语音工程。保存后，下次启动生效。

希望外部 Agent 和自己一起改项目时，可以让它先查支持的组件，做一项改动，再在 Forge 的「试玩回放」查看操作和状态。独立工具要调用模型，可以直接解压 `Redflag-AI-Runtime-SDK.zip`，运行其中的控制台示例，或在自己的 C# 程序中引用程序集。[MCP 接入](../scripts/redflag_mcp/README.md) · [Forge 试玩与审查](Creation-Review.md) · [SDK 用法](AI-SDK-Package.md)

瓦力没来扶人时，播放场景时选中瓦力，从属性的「AI 角色 → 编辑角色资产 → 候选」查看救援一项。玩家仍站着时，这里会显示「目标尚未倒地」。玩家倒地后再看，可以读到目标和距离。「最近一次提问的候选」保留发问时的那一刻，方便对照当时的决定和现在的局势。[Forge 实机界面](../output/validation/teammate-candidates/README.md)

## 验收记录

在 Forge 的「场景 Scene」里选中玩家或瓦力，视口上的「碰撞体」选「选中」，黄色轮廓就是物理系统使用的外形。到属性面板展开「3D 胶囊碰撞体」，可以拖动半径、总长和中心偏移。总长包含两端的圆帽。停在编辑模式下保存场景，重新打开后尺寸会保留。想连同地面、箱子一起看，把显示范围改成「全部」。

角色站着时，胶囊沿本地 Y 轴竖起，蹲下后，顶端随头部降低，底端仍贴着地面。倒地时，背甲靠近地面，胶囊转到本地 Z 轴，覆盖头到靴子的长度。站姿大小由各角色自己的碰撞组件决定，两种姿势的尺寸可在玩家脚本中调整。袋子、肘部和手指的细节仍由模型及交互系统处理。[角色接地实测](../output/validation/packaged-watt/ground-contact/README.md)

玩家和瓦力各演示了拾取、递药、维修、扶人和给自己用药。看递药时，可以留意一只手伸出、另一只手接住、物品换手以及两人收手的先后。录像保留了瓦力的游戏语音；每一章都能对照生效那一帧的手部位置、库存或生命变化。[十段侧面实机与逐帧记录](../output/validation/packaged-watt/hand-actions/README.md)

走路时，只看角色的位置很容易漏掉滑步。新版把画面、按键和骨骼位置一起记录，可以对着地面看蓝色机器人的靴底，也可以从侧面看左拳何时伸直、何时收回。逐项记录里列出了玩家、瓦力和巡检机各自动作的验证方式。[新旧实机录像](../output/validation/packaged-watt/animation/after-ik/compare.mp4) · [角色动作检查表](Station-Animation-Coverage.md)

完整试玩在开发机上从新目录解包，逐项校验清单，通过游戏窗口输入中文、移动和重新开局。离线服务另用三个空闲端口启动，确认使用的是包内的模型、解释器和语音识别程序。SDK 的独立测试也做了真实的文字、图像和语音调用。第二台 Windows 电脑的安装及实际操作手感仍待试玩。[游戏运行文件](../output/validation/packaged-watt/release-runtime.json) · [独立 SDK 测试](../output/validation/runtime-ai/portable-sdk.json)

输入同一句中文后，从游戏登记文字到开始派发模型请求，Redflag 的 20 次中位数为 0.128 毫秒，UE 样例的 5 次中位数为 9 毫秒。此时模型还没有作出决定。两个样例能执行的游戏动作不同，完整任务的用时仍缺少可直接比较的样本。[队友延迟与 UE 对照](../output/validation/runtime-ai/latency.md)

## 从源码重新打包

修改了瓦力的动作或场景后，在 Windows 构建说明所列的开发环境中生成 Release x64 版 Forge 及项目脚本。下面第一条命令检查 Windows 可执行文件实际依赖的 DLL，把它们放进一个空目录。第二条从场景出发收集模型、贴图、脚本和玩家手册。已经准备好的队友离线包可以继续使用。[Windows 构建说明](Build-Windows.md)

```powershell
python scripts/stage_windows_runtime.py tmp/watt-player
bin-int/redflag-mcp/Scripts/python scripts/asset_bundle.py RedflagForge/assets/scenes/Watt-Station.redflag output/releases/Redflag-Watt-Station-Win64.zip --runtime tmp/watt-player/RedflagForge.exe --companion --guide output/pdf/Redflag-Watt-Guide.pdf
```

需要重建离线包时，给 `scripts/package_companion_runtime.py` 提供同版本的 llama.cpp、Qwen 文本与视觉权重、嵌入版 Python 3.13.12 x64、语音专用环境的 site-packages，以及含识别 DLL 和模型的语音目录。运行 `--help` 可查看各路径参数。生成的压缩包内有原始许可、模型校验值和 Python 包版本。[打包脚本](../scripts/package_companion_runtime.py) · [Qwen 模型](https://huggingface.co/Qwen/Qwen3-VL-2B-Instruct-GGUF) · [llama.cpp b10887](https://github.com/ggml-org/llama.cpp/releases/tag/b10887) · [嵌入版 Python](https://www.python.org/downloads/release/python-31312/)
