# 瓦特的试听和游戏播报

在 Forge 的「角色创作」面板打开瓦特，进入「试答与试听」。保留默认台词「我在这里等你。」点击「试听」，可以听到 2.56 秒的 Dylan 录音。面板在成功打开 Windows 播放设备后显示「正在试听」。本次用同一个面板按下「停止试听」，确认状态随之更新。[面板实拍](forge.png) · [试听录音](../../../RedflagForge/assets/audio/watt/preview.wav)

发行包 `Redflag-Watt-Contact-03.zip` 中的八条 WAV 与开发目录逐字节相同。用这个包运行瓦特拾取、递药、自用药、维修与救援五次操作，逐次比较游戏交给声卡的数据与对应录音。递药一局在第 62 帧请求播报，下一帧提交 16 kHz 单声道声音，第 65 帧播完 25,600 个采样，即 1.60 秒。[各动作的播放回执](packaged.json)

另一局用真实通讯窗口打断这句话。玩家库存从 1 变成 2，听见开头后按 T 收起通讯，声卡报告停止在第 5,944 个采样，约 0.37 秒。记录的 WAV 保留提交给声卡的整句，回执给出实际播到的位置。单独把合成服务端口设成空端口时，原生播放测试也收到了相同的 WAV。[游戏事件](game/station-motion.tsv) · [打断时的设备读数](game/media.jsonl) · [离线设备读数](native-device.jsonl)

要听表里没有的台词，开发机需要另启带 Delivery 模型环境的语音服务。这次通过 HTTP 送入「钢板已经固定，可以继续检查左侧的管线了。」，Dylan 生成 4.72 秒。音色列表同时给出机器上安装的 Huihui、Zira 与瓦特的音色文件；选择 Huihui 读同一句试听词，也得到与瓦特不同的录音。[临时合成的台词](free-text.wav) · [服务读数](service.json) · [八句录音的模型版本和样本数](bake.json)

在仓库根目录运行以下命令可以复查面板与游戏。第二行的运行目录需要事先选一个新的空目录，将示例路径换成本机绝对路径。在线合成所需的 Delivery 项目位置与模型环境见[声音资产说明](../../../RedflagForge/assets/audio/watt/README.md)。

```powershell
tests/run_behavior_tree.cmd --voice-image output/validation/watt-voice/forge.png
tests/run_scene_tests.cmd --playtest-audio D:/Cdev/Redflag/tmp/watt-native-rerun --watt
python tests/check_watt_station.py --exe bin/Release-windows-x86_64/RedflagForge/RedflagForge.exe --recorded-speech output/validation/watt-voice/game
```
