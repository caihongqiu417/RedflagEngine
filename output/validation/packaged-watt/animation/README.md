# 维修站角色动作

[旧包实机录像](before/game.mp4)从出生点开始。按住右键，把镜头移到蓝色玩家侧面，松开后连按两次 F。旧版里手仍垂在身边。随后按 D 向右走，保持 D 再按 Shift 加速，最后松开 D 改按 W。可以同时看玩家和左后方黄色瓦特的双脚。镜头的末尾还有一次原地跳跃和一次蹲伏。[旧包的拳部连拍](before/punch.jpg)与[行走、加速和转向](before/gait.jpg)都取自同一次游玩。

这次跑的是 Windows x64 Release 包内的场景、模型、游戏脚本和可执行文件。游戏窗口与 HUD 为 1920×1080，三维场景使用玩家可以自行选择的 720p。镜头借助游戏里的鼠标右键转动，键鼠通过试玩记录器送进游戏。录像每张照片保留到下一张实拍的时刻，共 180 张、10.832 秒，图片之间没有补帧。截图与游戏状态的读数前后可能相差一帧或更多，取样的两个帧号都在 take.json 中。

旧包中玩家向右走的 1.01 秒共移动 2.74 米，加速后的 0.70 秒移动 2.51 米。转弯前经过箱子，地形与物体会限制实际位移。瓦特跟了 4.33 米，玩家原地跳起后上下高度相差 1.12 米。这些是世界坐标中量出的实际距离。旧包的试玩记录没有骨骼字段，视频里能看见手脚，却无法从这份日志读出当时的剪辑和关节坐标。[位置与按键的逐帧原文](before/frames.jsonl.gz) · [输入及窗口截图帧号](before/take.json) · [汇总数据](before/measured.json)

为了查清旧角色两次按 F 时到底在播什么，另做了一次诊断。保留旧包里的关卡、模型和 C# 程序，将原生程序换成可观察骨骼的 Release 构建（SHA-256 前 12 位 a88c7f68c1b0）。两次按键时主角都在播放 Idle-cycle，左手指节相对髋部的最大前移分别为 0.002 米、0.029 米。玩家走、跑时分别读到了 walking_gun-cycle 和 running_gun-cycle，瓦特跟随时读到 walking_nogun-cycle。这组数字包含新原生程序的姿势处理，只用于查旧脚本、旧模型在同一原生程序下的表现。[额外诊断的逐帧骨骼](old-game-new-native/frames.jsonl.gz) · [采样记录](old-game-new-native/measured.json)

## 在另一份发行包上复测

从仓库根目录运行。第一条命令解压到临时目录，打开游戏，沿同一条路线操作；第二条读取这次保存的照片和状态，生成连拍及 MP4。需 Windows、Python、项目已有的 pywin32 和 Pillow，以及 NumPy 和 PATH 中的 FFmpeg。

~~~powershell
python tests/capture_player_motion.py output/releases/Redflag-Watt-Station-Win64.zip tmp/station-motion-take
python tests/review_player_motion.py tmp/station-motion-take tmp/station-motion-review --require-animation
~~~

加上 `--require-animation` 时，两次出拳都要有起手、接触检查和收拳记录，手还要相对髋部实际前伸并回收。行走和奔跑时，脚相对角色朝向应交替出现在另一只脚前面。脚尖接近地面的样本还保留了水平移动距离和采样间隔，便于另查滑步。3 厘米是本测试在平地上选取低位脚尖的高度范围。录旧包时省去这个开关即可检查位移和录像。主角是实体 1101，瓦特是 1102，原始状态按实体编号关联到同一帧。
