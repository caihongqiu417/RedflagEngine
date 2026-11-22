# 从压缩包启动伙伴模型

测试将 `Redflag-CompanionRuntime-Win64.zip` 解压到一个新建目录，把游戏使用的八个服务脚本复制到旁边的 `scripts`，再运行正式的 `Start-NpcServices.ps1`。启动前清除本次进程继承的模型地址和 Python 配置，选择三个空闲端口。启动后同时检查监听端口、进程的可执行文件和命令行，确认两个服务使用解压出的 Python，推理使用同目录内的 llama、Qwen 权重和视觉投影文件。测试结束只关闭这次解压目录里的三个进程。

柜门锁着时，模型在杯子和钥匙之间选了钥匙。识图用同一张游戏场景图做两次提问，第二次把红、蓝通道交换。Qwen 两次分别回答「红色」和「蓝色」。合成使用本机 Windows 的普通话音色 Microsoft Huihui Desktop，把「请打开仓库的门。」生成单声道 WAV，再直接把这些字节交给包内的 Zipformer 识别。可以对照原句、实际转写和 WAV，查看这次识别的多字现象。[实际回复与 SHA-256](check.json) · [原始合成语音](synthesized.wav)

从仓库根目录运行 `python -X utf8 tests/check_companion_archive.py output/releases/Redflag-CompanionRuntime-Win64.zip` 可以复测。验证脚本用开发机的 Pillow 交换颜色，用 psutil 核实监听进程。游戏服务本身用包内解释器。语音合成依赖 Windows 已安装普通话音色，这次验证使用文件传递音频。[验证脚本](../../../tests/check_companion_archive.py)
