# 发出指令、角色动作和语音

玩家说「帮我留一份医疗包，等我过来再给我」。要测出玩家等了多久，需要先约定结束点。瓦特选中“留药”时，背包里的数量还没有变化；队友站到手边后，瓦特会伸手，把药交出去，收回手，然后说出回执。这些时刻在程序里可以分别观测。下面每个百分位数都来自逐次计时。P50 表示中间一次，P95 使用排序后相邻两次的线性插值。每组 20 次适合在本机复测，尾部两个样本就能明显影响 P95。

## 维修站里的动作

Windows 上启动 Debug 版 RedflagForge，运行 Vulkan 的正式 Watt-Station 场景。游戏客户区为 640 × 360，玩家与瓦特使用关卡的起始位置。测试脚本向当前游戏的 `Npc.Submit` 发同一句话，再读本次需求的行为树选择和动作。瓦特有一份药，离玩家超过两米，两次发令之间取消等待，使药仍在背包。20 次都选择了 `reserve` 并进入 `wait`。两者沿用待机剪辑，因而这个结果衡量游戏已经开始留药。

再单独要求「带着医疗包，马上走到我身边把它给我」。选择器需要选中 `share`，并且脚本必须实际读到角色在地面上移动超过一厘米，才算起步。读数在游戏更新中取得，显示器扫描到像素还需要后续的绘制与显示时间。每轮记录后还原瓦特的位置，再发下一条。20 次均观察到起步。[留药逐次数据](forge-reserve-latency.json) · [起步逐次数据](forge-walk-latency.json) · [游戏里的计时脚本](../../../tests/StationLatency.cs)

| 从 C# 发令算起 | P50 | P95 |
|---|---:|---:|
| 瓦特接到命令，开始原地留药 | 748.61 ms | 909.64 ms |
| 瓦特朝队友实际走出一厘米 | 692.63 ms | 752.86 ms |

两条命令有各自的角色行为，因此两行耗时也分别解释。此组较早的留药样本采集期间，同机另有一次短时窗口输入回归；逐次数据保留了当时的负载。

## 从一段录音到听见回执

用独立的 C++ 宿主建一个只有瓦特与队友的场景，把队友放在瓦特正前方一米。录音松开时，宿主把完整 WAV 交给同游戏共用的 `SpeechInput::SubmitWav`。本机语音服务转写后，宿主把识别到的文字原样传给 `NpcSystem::Submit`。真实模型选择留药，此时队友已经靠近，于是瓦特直接开始递药。宿主以真实时钟每秒推进 60 次，使用原动作表的接触帧实际转移库存。它随后沿用游戏的语音调用，读到带台词的“完成”事件才启动 Windows 合成。本组没有 GPU 绘制；一次计时覆盖 ASR、语言模型、行为树、动画时序、库存和 Windows 音频设备。输入录音的 4.555 秒播放长度在计时之前。[录音与转写](medicine-voice.json) · [真实引擎入口](../../../tests/NpcVoiceLatency.cpp) · [正式界面播音调用](../../../RedflagForge/src/CompanionPanel.cpp)

| 从交出 WAV 算起，20 次均完成 | P50 | P95 |
|---|---:|---:|
| 识别器交回全文 | 238.68 ms | 257.34 ms |
| 模型选择通过，开始递药 | 453.71 ms | 523.60 ms |
| 手接触队友，库存改变 | 1187.26 ms | 1257.61 ms |
| 递药动作播完，游戏收到回执 | 2454.08 ms | 2521.15 ms |
| Windows 输出设备读到第一个有声样本 | 3239.08 ms | 3293.51 ms |

本组模型为 llama.cpp b10887 上的 Qwen3-VL-2B-Instruct，Q4_K_M，单个请求可用 8192 个 token。语音使用本机识别运行库和 Microsoft Huihui Desktop。在这次流程里，递药动作持续两秒，约 0.75 秒时交接；回执恰在动作结束后才提交。若将来希望玩家先听到「我来递给你」，可以在开始动作时播放另一句确认，物品真正交出后仍以库存事件为准。当前样本中的两秒有明确的游戏意义。[动作表](../../../RedflagForge/assets/ai/watt.animator.yaml) · [20 次语音记录](engine-voice-8k.json)

## 打断后还会听到多少声音

在使用 Windows 输出设备的原生入口，连续 20 次从请求合成到设备读到有声样本为 717.01 / 784.58 ms（P50 / P95）。立刻调用 `Stop` 并等待它返回，用时 9.71 / 14.02 ms。这里的终点是 Windows 返回音频重置结果。没有用麦克风测扬声器的余响。[逐次记录](playback-20.txt) · [播放回归](../../../tests/SpeechPlaybackTests.cpp)

语音房还经过 WebRTC 接收与解码。另一组 20 次试验先连续收到三个有声音的音频块，再发送取消，从接收端直接检查此后的采样值。取消回执只需 0.88 / 1.28 ms，最后有声音的音频块到达时已经过了 117.46 / 126.22 ms。接下来持续安静的第一个音频块在 139.41 / 143.01 ms 到达。每次取消后第 400 到 700 毫秒间的样本均为零。这里“安静”指解码后每块采样的归一化均方根振幅小于 0.002。计时包含本机编解码与接收端音频排队。[取消与解码音量](webrtc-stop-20.json) · [测量脚本](../../../tests/benchmark_speech_stop.py)

## 同机 Unreal 包

在同一台 RTX 5070 Ti Laptop GPU 笔记本上运行 Delivery 的 `lan-playtest-009` Windows 包，地图为正式 `TestForCharacter`，640 × 360，实际 D3D 绘制、60 FPS 上限。通过游戏的 `DeliveryConversationComponent::Submit` 对居民发同一句留药请求。游戏在同一帧登记这句话并请求播“正在听”的反应。这个标记表明游戏收到输入，角色接下来仍要向模型询问怎样回应。

原样使用 8192 token 的同一模型服务，两个独立游戏进程中，各看到同一条玩家发言的两次模型请求。模型接口都返回 HTTP 200，但四条都在 Delivery 的 `validate_action` 被拒。第二轮从实际抛错处读取字段，两条回答分别为 38、20 个字，均缺少必填的 `emotion`。该入口只接受 `Calm`、`Angry`、`Surprised`。模型发回的候选和台词走到此处，动作还没有交给游戏，因而本组有效首动作与玩家语音都是零个样本。两个进程的首次玩家请求分别在交谈登记后 4.233、2.094 秒发出，并分别在 2.972、4.099 秒后被拒。NPC 的背景动作另有请求 ID，不能算到这句话上。[原始标记](ue-validation-8k.json) · [具体失败字段](ue-format-8k.json)

将同样的包切回每请求 4096 token 的本机模型服务，8 次模型请求均返回 HTTP 400。模型实际收到的输入长 4677 到 5827 token，在生成答案之前就超过了容量。这个错误和 8192 组的格式错误发生在不同位置。[4K 的模型错误与长度](ue-format-4k.json)

最后保持 8192、同一权重、同一现场和玩家原话，只在测试代理里要求模型按三个字段返回首条记录，候选、情绪和台词仍由模型自己选。正式游戏收到了有效模型结果。交谈登记后 5.006 秒才开始本轮玩家请求，又过 6.032 秒，游戏批准了它，合计 11.038 秒。这次只有一个匹配的玩家请求，不计算 P95。UE 排入播放的两句台词对应的音频字节均为 0，也没有记录到该轮的录制音频开始播放。这里无法给出 UE 播音停止时间。程序仅在实验的推理请求中加了格式限制，原来的 UE 包和后台程序仍保持其验证行为。[格式对照的游戏日志](ue-guided-8k.json)

这份地图里，居民这次可选的是等待、看向人物、走动等现场反应，清单里没有用于为队友保留药品的命令。格式对照中模型选的是 `Wait`。维修站的队友本来就有库存和递药动作，两边这条输入所能触发的功能并不等价。依据上述结果不能给出两款引擎在完整留药任务上的速度比。

## 复测

从 Redflag 仓库根目录启动本机语音服务、模型与正式 Debug 构建。正式窗口使用已启动的本机 8716 和 8717；独立语音宿主另起仅在测试期间存在的决策网关，将它指向与 UE 相同的模型实例。UE 命令的两个本地输入目录分别指向已打好的 Windows 游戏包和它对应的 DeliveryAI 工具目录。

```powershell
python tests/check_watt_station.py --latency reserve --trials 20
python tests/check_watt_station.py --latency walk --trials 20
python tests/benchmark_npc_voice.py --endpoint http://127.0.0.1:18158/v1/chat/completions --out output/validation/runtime-ai/engine-voice-8k.json
bin-int/Tests/Decision/SpeechPlaybackTests.exe 8717 20 > output/validation/runtime-ai/playback-20.txt
tmp/ai-media-venv/Scripts/python.exe tests/benchmark_speech_stop.py --out output/validation/runtime-ai/webrtc-stop-20.json
python tests/benchmark_packaged_ue.py --windows $Package --delivery-tools $DeliveryAI --model-endpoint http://127.0.0.1:18158/v1/chat/completions --seconds 34 --out output/validation/runtime-ai/ue-format-8k.json
python tests/summarize_npc_latency.py
```
