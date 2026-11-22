# 试玩中听到的队友回执

正式运行维修站，瓦特走向玩家，伸手把医疗包从自己的背包交到玩家手里，然后播报「医疗包给你了」。试玩记录只保存这句由游戏合成并交给 Windows 的声音。它不会打开麦克风，也不会采集桌面上其他程序的声音。未启用 `REDFLAG_PLAYTEST` 的普通游玩不写媒体文件。[游戏动作](station/station-motion.tsv) · [本次语音 WAV](station/speech-7-1102-4.wav)

录音文件包含完整的合成语音。回看时，先在 `media.jsonl` 中找到这句话的 `id`，再看最后一次播放事件。本例每秒 16000 个单声道采样。第 9 帧提交了 34560 个样本。第 10 帧检查时，设备已读到第 4852 个样本，越过句首静音。第 12 帧游戏接受第二次按 T，收起队友通讯；停止前读到第 14700 个样本，约为 0.919 秒。因此听这次实际提交的片段时，应在原文件的 0.919 秒处结束。数值来自默认 Windows 播放设备的读取位置，播放器音量和扬声器输出未录制。[逐帧媒体记录](station/media.jsonl)

每条记录的 `entity` 是说话者，`seq` 和 `seconds` 指向本局的游戏帧，`generation` 标记当时由谁接管操作所对应的版本。语音还在合成时取消，只出现 `requested` 和 `cancelled`；提交设备后出现 `submitted`，并给出相对本局目录的 WAV 文件名。越过句首静音后出现 `first_voice`，播完为 `finished`，中途打断为 `stopped`。`device_samples` 为 null 表示该时刻没有可读取的设备位置。写入失败的音频不会附一个不可用的文件名。[设备读取与打断](../../../Redflag/src/Redflag/AI/SpeechPlayback.cpp) · [试玩音频记录](../../../Redflag/src/Redflag/Debug/PlaytestAudio.cpp)

从仓库根目录执行。首条命令用独立宿主检查无会话、提前取消、接管后打断和正常结束；为它指定一个新目录。第二条启动正式 Vulkan 维修站及游戏脚本，在队友通讯中实际打断回执。本机录制版本 SHA-256 为 `d8e7e7465a287c1d9075ebe6678c1ee3d4738aa3e3d6b818c70d27cf937858dc`。

```powershell
tests/run_scene_tests.cmd --playtest-audio D:/Cdev/Redflag/tmp/playtest-audio-new
python tests/check_watt_station.py --recorded-speech output/validation/playtest-audio/replay
```
