# 维修站机器人

角色模型、骨架、动作和贴图来自 Godot 的 Third Person Shooter Demo。作者为 Juan Linietsky 和 Fernando Miguel Calabró，原作于 2018 年按 CC BY 3.0 发布。Redflag 将样例材质引用的颜色、法线、金属度、粗糙度与自发光贴图装入 GLB，保留骨骼动画，并移除了作者标为试作的部分动作。具体版本和文件校验值见同目录的 sources.json。[Godot 第三人称样例](https://github.com/godotengine/tps-demo/tree/a82f15448e9b015440d3bbdf5e10801b260c4e9f) · [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/)

瓦特弯腰取物时用的 `Station-Pickup` 改编自 Quaternius《Universal Animation Library 2》的 `Farm_Harvest`。作者的角色用右手拿东西，这个机器人右臂带有设备，因此导入时交换左右骨骼，在角色正中面对称动作。两者臂长、手指长度不同，导入器保留脚尖接地高度，并在机器人手指接近地面时调整腕部角度。递物时用的 `Station-Give` 来自第一册的 `Interact`，采用原来的左手前伸动作。两段原动作都按 CC0 发布。[第一册](https://quaternius.com/packs/universalanimationlibrary.html) · [第二册](https://quaternius.com/packs/universalanimationlibrary2.html) · [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/)

在仓库根目录运行下面的命令可以重新导入。先从作者两册的 Standard 压缩包分别解出 Unreal-Godot 目录里的 `UAL1_Standard.glb`、`UAL2_Standard.glb`，放到 `tmp/animation-sources/`。原始文件的 SHA-256 在 `sources.json` 中。Godot 模型和贴图由导入脚本按指定提交下载。NumPy 和 SciPy 用于离线骨骼旋转计算。

```powershell
python -m venv tmp/retarget-env
tmp/retarget-env/Scripts/python -m pip install numpy scipy
tmp/retarget-env/Scripts/python scripts/import_station_robots.py
tests/run_scene_tests.cmd --npc-animation
```

最后一条命令用引擎自己的 `ModelAnimation::SamplePose` 和蒙皮矩阵检查两段动作。沿角色朝向是模型的 +Z，竖直方向是 +Y。取物时一共 2.5 秒，左腕在第 36 帧走到最低点，离地约 13 厘米。递物时一共 2 秒，左腕在第 22 帧伸到最前方。两段动作每秒 30 帧，库存分别在 1.20 秒和 0.73 秒时尝试更新。这两个时间写在角色的动画状态表里。物体已被别人拿走或队友已经离开时，引擎仍会重新检查距离和库存。[动作表](../../../ai/watt.animator.yaml) · [离线重定向](../../../../../scripts/retarget_station_motion.py) · [引擎采样与手腕位置验证](../../../../../tests/NpcMotionTests.h)
