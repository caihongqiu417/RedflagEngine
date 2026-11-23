# Redflag AI Runtime SDK

先试着给一把钥匙写说明。解压发布包后，在这个目录打开 PowerShell。以下命令使用你已经启动的模型服务，把地址和模型名称换成自己的配置。运行网关需要 Python 3.10 或以上；采用聊天服务连接时，Python 使用标准库。

```powershell
# 在解压目录运行。支持图片的上游模型加 -Vision。
.\scripts\Start-NpcServices.ps1 -SkipSpeech -RequireAll `
  -ModelEndpoint http://127.0.0.1:18157/v1/chat/completions -ModelName Qwen3-VL-2B-Instruct -Vision
.\ModelConsole.exe "为维修站的柜门钥匙写一句物品说明。"
```

控制台程序在 Windows 的 .NET Framework 4.7.2 或 4.8 上运行。也可以从自己的 C# 项目引用包根目录的 `Redflag-ScriptCore.dll`，按 `examples/Program.cs` 调用 `Redflag.ModelConnection`。带图提问时，在问题后依次加上端口和图片路径。例如 `ModelConsole.exe "图里有什么？" 8716 "workshop.png"`。按 Ctrl+C 可以打断本轮回答。变量、端口、取消和返回值见《从游戏或编辑器调用模型》。[C# 及 HTTP 接口](Model-Connection.md)

编写其他语言的工具时，先向 `GET http://127.0.0.1:8716/v1/health` 查看模型名称和能力列表。`POST /v1/replies` 让模型生成文字，`POST /v1/decisions` 让模型从调用者给的编号中选择。C# 的 `Npc`、`GameWorld` 等游戏接口需要运行中的 Redflag 主机。独立工具可直接使用模型接口，或用 HTTP 实现自己的客户端。

如果项目在 Redflag 内运行，可以再打开 `examples/npc-memory/SurveyNotes.cs`。勘察员记下的是亲眼见到的阀门或玩家转述的地点，道具是否还能操作仍由当前场景判断。两个 YAML 描述该角色和允许的动作，`MemoryHost.cpp` 展示如何在原生引擎宿主中装载示例。这个例子需要完整的 Redflag 主机与物理场景。仅想在其他应用里请求一段文字或分析一张图片时，使用前面的 `ModelConsole` 示例即可。

## 语音与依赖

如需把普通话录音转成文字，另准备含 `DeliverySpeechRuntime.dll` 和 `models` 的识别运行时（ABI v1，已验证 sherpa-onnx Zipformer）。先安装脚本列出的三个固定版本的 Python 包，再启动语音服务。已验证的组合是 Windows x64、Python 3.13、aiortc 1.16.0、aiohttp 3.14.5 和 webrtcvad-wheels 2.0.14.post1。系统语音播报使用 Windows 自带的 System.Speech 和已安装的音色。

```powershell
# 在解压目录运行，将最后的目录换成识别运行时所在位置。
py -3.13 -m venv tmp/ai-media-venv
tmp/ai-media-venv/Scripts/python -m pip install -r scripts/requirements-speech.txt
.\scripts\Start-NpcServices.ps1 -SkipDecision -RequireAll -SpeechRuntime "C:/MyModels/speech-runtime"
```

| 本机 8717 服务 | 输入与返回 |
| --- | --- |
| `POST /v1/speech/transcribe` | 16 kHz 单声道 PCM16 WAV，一次不超过 15 秒。返回 `{"text":"…"}` |
| `GET /v1/speech/voices` | 返回 `voices` 和 Windows 实际使用的 `default_voice` |
| `POST /v1/speech/synthesize` | 传 `{"text":"你好","voice":""}`，空音色使用默认值。返回 `audio/wav`。每次 1 至 300 个字符 |
| `POST /v1/speech/streams` | 接收同一组文字与音色。返回 HTTP 201 和 `{"id":"…"}`，可以立即用编号取声音。 |
| `GET /v1/speech/streams/{id}` | 200 返回一块 16 kHz 单声道、16 位小端 PCM。202 表示这一块还在合成，可继续取。204 表示已取完整句。每个编号由一个播放器按顺序读取。 |
| `DELETE /v1/speech/streams/{id}` | 打断这一轮，在服务端取消正在合成的台词。播放器同时清空自己的声卡队列。 |
| `POST /v1/speech/rooms` | 创建房间，返回 `room` 编号。在浏览器打开 `/room/{room}`，允许麦克风后按住说话 |
| `GET /v1/speech/rooms/{room}` | 取走上次读取以来的转写。宿主每 200 毫秒读取一次，将文字交给创建这个房间的玩家或角色 |
| `DELETE /v1/speech/rooms/{room}` | 关闭房间和连接。接收方丢弃已经退出的游戏会话 |
| `POST /v1/speech/offer` | 用 `type`、`sdp` 和可选的 `room` 交换 WebRTC 会话描述。数据通道发送转写、播报和取消事件。调用方式见 `scripts/speech_client.py` |

语音服务在 `voices` 中用 `bank_speech: 4` 表示支持分块读取。使用支持分块的 Delivery 模型时，第一块声音就能交给播放器。Windows 系统音色和只会返回完整录音的模型，仍可通过同一个入口取到整句。已有编辑器工具可继续用 `synthesize` 取得完整的 WAV 文件。处理 404 或 405 的旧服务时，引擎自动改用这个接口。

在 Redflag 源码根目录运行 `tests\build_decision_client.cmd`，再用安装了语音依赖的 Python 运行 `tests/check_speech_blocks.py`。测试源先交出前半句，等 Windows 默认播放设备确实读到有声采样后，再放行后半句。测试同时比较录音里的 9600 个采样，并在上一句合成未结束时切换到下一句。这个检查验证播放、取消和录音的先后顺序；实际音色的生成耗时需另用本机模型测量。

同一项测试还用只提供整句的旧服务播报，试过损坏的 WAV 和生成到半途才出错的请求。播放包内台词时，服务收到的请求数保持不变，录音与原始 WAV 逐字节相同。独立 SDK 可用 `tests/check_packaged_ai_sdk.py --speech-only` 复测，另给 `--sdk`、`--speech-runtime` 和 `--speech-python` 指定发布包、识别运行时和 Python 路径。在空目录解压的实测中，使用系统普通话音色，SDK 能取完整 WAV、通过新接口接收 21840 个采样，也能把录音识别回文字。[原生播放测试](../output/validation/runtime-ai/speech-stream-v4/playback.txt) · [独立包结果](../output/validation/runtime-ai/portable-speech-v4.json)

正式试玩记录中的 `first_voice` 表示设备已经读过有声采样。若一条台词尚在生成，声卡已播完手头的声音，下一块提交时会记一条 `buffer_gap`。其中 `gap_ms` 从游戏帧第一次发现队列为空算起，因此可能短于实际停顿。需要回听时可打开这条记录旁的 WAV，结合 `device_samples` 查看打断前真正播到了哪里。

发布包按文件清单组装。模型权重、API 凭证、用户录音和场景资产由使用者自行提供。Redflag 代码沿用项目根目录的 Apache-2.0 许可。附带的 `mono/etc/mono/4.5/machine.config` 来自 Mono 6.12.0.182；嵌入 Mono 的宿主要同时提供配套的 `mono/lib` 并在初始化时设置配置目录。Windows .NET Framework 的示例使用操作系统已有的运行库。可选的 Python 包由 pip 安装，许可证随各自发行包提供。[Redflag 许可](LICENSE) · [Mono 许可](licenses/Mono-LICENSE) · [Mono 原始配置](https://github.com/mono/mono/blob/mono-6.12.0.182/data/net_4_5/machine.config)
