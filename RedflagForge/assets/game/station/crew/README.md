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

## 行走和蹲行

Godot 的原动作每迈完左右两步，名为 `root` 的节点也向前走一个完整步幅。引擎可以据此把游戏里的移动距离换算为动画时间。例如，角色走出 0.875 米，普通行走便应该播完半个循环，原先在前面的左脚此时已经移到右脚后面。把角色的位置交给游戏移动后，绘制骨骼时要把 `root` 固定在角色身上，否则同一段路会走两遍。持炮和徒手的四段走跑剪辑与上游文件逐轨比对，采样时间和数值都相同。

| 剪辑 | 一个循环 | 向前步幅 | 按原速播放时的移动速度 |
| --- | ---: | ---: | ---: |
| `walking_nogun-cycle` | 1.250 秒 | 1.750 米 | 1.400 米/秒 |
| `running_nogun-cycle` | 0.833 秒 | 2.667 米 | 3.200 米/秒 |
| `Station-CrouchWalk` | 2.000 秒 | 1.453 米 | 0.727 米/秒 |

蹲姿和蹲行分别来自 Quaternius 的 `Crouch_Idle_Loop` 和 `Crouch_Fwd_Loop`。原作者让角色在原地迈步，脚贴地时会相对身体向后移动。导入器取每秒 30 帧的机器人动作，把脚尖离最低点不到 2 厘米且正向后移动的时刻作为支撑期，用脚尖后移速度的中位数估计前进速度，再为 `root` 写入两秒的前进轨迹。2 厘米是本次导入区分贴地和抬脚的筛选距离。蹲下时还要算上靴底的厚度，若只把脚尖骨放到地面，靴跟在这段动作中会穿地近 11 厘米。导入器按整个靴子的实际蒙皮顶点校正高度。引擎以 60 Hz 复查了关键帧以及相邻两帧之间的姿势，蹲行时最低顶点在地面以下 0.44 毫米至地面以上 3.35 毫米之间。[动作原件与许可](https://quaternius.com/packs/universalanimationlibrary.html)

## 出拳

Godot 的原模型有举炮和蓄力。新剪辑 `Station-Punch` 采用 Quaternius 的左手动作 `Punch_Jab`，把肩、肘、腕和手指关节映射到机器人身上。伸腕时要同时保留掌心朝向和指节的弯曲，否则伸出的仍是一只张开的手。机器人在第 6 帧（0.20 秒）伸到最远，整段在第 26 帧回到起手姿势。此时肩、肘、腕沿角色前方的位置分别是 0.135、0.334 和 0.704 米。

验证时从起手姿势选出左手中指第一节附近 4 厘米内的 429 个网格顶点。动作播放到第 6 帧时，这些顶点平均前移了 0.737 米，手腕前移了 0.551 米。骨骼轨迹和蒙皮后的手都由引擎直接采样。实际交互是否命中，还需由游戏检查角色当时面对的目标与距离。[原动作](https://quaternius.com/packs/universalanimationlibrary.html) · [引擎回归](../../../../../tests/NpcMotionTests.h)
