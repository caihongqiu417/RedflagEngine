# 潮汐维修站

进入维修站后，你操控蓝灰色机器人。左前方的赭色机器人叫瓦特。按 **T** 打开队友通讯，点击输入框，试着说「把你背包里的一份医疗包送给我」。瓦特会选择动作，走到够得着你的位置，递出药。留意左下角的医疗包数。交接前你们各有一份，交接后你有两份。[玩家与创作手册](../output/pdf/Redflag-Watt-Guide.pdf) · [发布包递药实测](../output/validation/packaged-watt/release-delivery/result.json)

## 开始游戏

交付目录为 `output/releases`。本机已经解压好，打开 `Watt-Station-Win64`，双击 `Play-With-Companion.cmd`。换一台电脑时，把 `Redflag-Watt-Station-Win64.zip` 与 `Redflag-CompanionRuntime-Win64.zip` 解压到同一个文件夹。启动脚本旁应当能看见 `CompanionRuntime`。需要 Windows x64、支持 Vulkan 1.3 的显卡驱动和足够的显存。要让瓦特用中文朗读，请在 Windows 安装中文音色。第一次启动时，程序需要把本地模型载入内存。[离线启动、看图和中文语音实测](../output/validation/packaged-companion/README.md)

维修棚里的应急发电机还没通电。你可以过去按住 **E** 维修，也可以告诉瓦特「去东面的应急发电机，把它修好」，自己先去西侧闸口。要等机器供电、两人都到闸口，任务才会结算。按住 **F1** 查看键位，结束后按 **R** 重新开局。一次发布包验收记录了两人撤离，另一次交战记录了瓦特两次扶起玩家、最终任务失败和重新开局。[任务经过](../output/validation/packaged-watt/release-mission/mission.json) · [逐帧动作与输入](../output/validation/packaged-watt/release-mission/frames.jsonl)

游戏默认 1920 × 1080。如果转动镜头时有明显迟滞，退出后双击 `Play-720p.cmd`，它会用 1280 × 720 绘制场景，保留原来的窗口、界面字号和队友通讯。在 RTX 5070 Ti Laptop 上固定出生机位，两种模式的帧时间中位数分别为 87.77 和 58.12 毫秒。720p 转动起来更快，目前两档都达不到稳定的 30 帧。[帧时间实测](../output/validation/packaged-watt/resolution.json)

## 修改队友

在源码目录运行 `Open-Vulkan-Editor.cmd --scene assets/scenes/Watt-Station.redflag`。到「队友通讯 → 角色创作」复制瓦特，试着把副本的「留药」连到「分享物资」，保存后再只换音色。重新打开副本，之前的连线应当还在。项目里的通讯、角色定义、行为树、C# 示例和 API 手册均可独立查看。[瓦特的角色文件](../RedflagForge/assets/ai/watt.npc.yaml) · [行为树](../RedflagForge/assets/ai/watt.tree.yaml) · [C# 维修动作](../examples/npc-runtime/RepairOrder.cs) · [引擎 API](../output/api/index.html)

希望外部 Agent 和自己一起改项目时，可以让它先查支持的组件，做一项改动，再在 Forge 的「试玩回放」查看操作和状态。独立工具要调用模型，可以直接解压 `Redflag-AI-Runtime-SDK.zip`，运行其中的控制台示例，或在自己的 C# 程序中引用程序集。[MCP 接入](../scripts/redflag_mcp/README.md) · [Forge 试玩与审查](Creation-Review.md) · [SDK 用法](AI-SDK-Package.md)

## 验收记录

完整试玩在开发机上从新目录解包，逐项校验清单，通过游戏窗口输入中文、移动和重新开局。离线服务另用三个空闲端口启动，确认使用的是包内的模型、解释器和语音识别程序。SDK 的独立测试也做了真实的文字、图像和语音调用。第二台 Windows 电脑的安装及实际操作手感仍待试玩。[游戏运行文件](../output/validation/packaged-watt/release-runtime.json) · [独立 SDK 测试](../output/validation/runtime-ai/portable-sdk.json)

输入同一句中文后，从游戏登记文字到开始派发模型请求，Redflag 的 20 次中位数为 0.128 毫秒，UE 样例的 5 次中位数为 9 毫秒。此时模型还没有作出决定。两个样例能执行的游戏动作不同，完整任务的用时仍缺少可直接比较的样本。[队友延迟与 UE 对照](../output/validation/runtime-ai/latency.md)

## 从源码重新打包

修改了瓦特的动作或场景后，在 Windows 构建说明所列的开发环境中生成 Release x64 版 Forge 及项目脚本。下面第一条命令检查 Windows 可执行文件实际依赖的 DLL，把它们放进一个空目录。第二条从场景出发收集模型、贴图、脚本和玩家手册。已经准备好的队友离线包可以继续使用。[Windows 构建说明](Build-Windows.md)

```powershell
python scripts/stage_windows_runtime.py tmp/watt-player
bin-int/redflag-mcp/Scripts/python scripts/asset_bundle.py RedflagForge/assets/scenes/Watt-Station.redflag output/releases/Redflag-Watt-Station-Win64.zip --runtime tmp/watt-player/RedflagForge.exe --companion --guide output/pdf/Redflag-Watt-Guide.pdf
```

需要重建离线包时，给 `scripts/package_companion_runtime.py` 提供同版本的 llama.cpp、Qwen 文本与视觉权重、嵌入版 Python 3.13.12 x64、语音专用环境的 site-packages，以及含识别 DLL 和模型的语音目录。运行 `--help` 可查看各路径参数。生成的压缩包内有原始许可、模型校验值和 Python 包版本。[打包脚本](../scripts/package_companion_runtime.py) · [Qwen 模型](https://huggingface.co/Qwen/Qwen3-VL-2B-Instruct-GGUF) · [llama.cpp b10887](https://github.com/ggml-org/llama.cpp/releases/tag/b10887) · [嵌入版 Python](https://www.python.org/downloads/release/python-31312/)
