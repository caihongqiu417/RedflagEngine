# 瓦力的声音

瓦力使用 Delivery 中为「二狗」配置的 `ergou` 音色。Qwen3-TTS 实际接收到的说话人名是 `Dylan`。模型版本固定在 `85e237c`，配置沿用 Delivery 的种子 20261113 与 CUDA Graph。模型生成 24 kHz 的单声道声音，导入时重采样为 16 kHz、16 位 PCM，供游戏的 Windows 音频设备和 WebRTC 播放。Qwen 发布的这个模型标明采用 Apache-2.0。[模型版本与许可](https://huggingface.co/Qwen/Qwen3-TTS-12Hz-0.6B-CustomVoice/tree/85e237c12c027371202489a0ec509ded67b5e4b5) · [作者的 CustomVoice 说明](https://github.com/QwenLM/Qwen3-TTS#custom-voice-generate)

打开 Forge 的「角色创作」，选择瓦力，在「试答与试听」中点击「试听」。默认台词是「我在这里等你」。八句常用台词各有一段已经生成的录音，和游戏一起发布，双击 WAV 也能试听。角色动作完成后，游戏用实际回执查对应的文件。例如瓦力说「医疗包给你了」时会播放 `share.wav`。[台词与录音文件](dylan.voice.json)

游戏优先播放台词表中的录音。瓦力现想现说的回答默认由 Windows 安装的中文语音朗读。要让新台词也使用 Dylan 音色，先准备带有 CustomVoice 模型和 Python 环境的 Delivery 项目，再在 Redflag 根目录运行以下命令，把路径换成本机安装位置。第一次合成需要载入模型。超过 120 字的回答会分段合成，再按原顺序播放。

```powershell
scripts/Start-NpcServices.ps1 -SkipDecision -QwenSpeechProject D:/Udev/Delivery
```

若要重新生成这八段录音，同样在 Redflag 根目录运行以下命令。生成器逐句核对模型版本和返回的说话人，将 WAV 的 SHA-256、样本数和耗时写入 `output/validation/watt-voice/bake.json`。

```powershell
tmp/ai-media-venv/Scripts/python scripts/qwen_speech.py --delivery D:/Udev/Delivery
```
