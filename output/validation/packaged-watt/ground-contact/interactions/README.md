# 倒地后的救援与物品交接

在本次货场发行包中，玩家走近后按住 E，扶起倒地的瓦特。第 58 帧恢复生命，第 74 帧瓦特已站稳。交换角色后，第 49 帧瓦特碰到玩家的手腕；第 66 帧玩家恢复站立。侧面实拍中两者都使用实际游戏的姿势与胶囊碰撞体。拍摄时移动了开始位置并固定观察相机，保留关卡里的地面和道具高度。瞄准、移动、库存和救援时间都由正常游戏脚本控制。[玩家的连续画面](player-frames.jpg) · [瓦特的连续画面](watt-frames.jpg)

第一次换上胶囊时，瓦特走到距倒地玩家 1.99 米的位置就停住了。原本从两名角色脚底连线检查是否有墙。当脚底真正落在砂石上，两端的高度只相差浮点计算的尾数，这条线就可能在地面内部。现在仍然按两人的位置计算能否走进交互范围，再从身体中心看向目标碰撞体中心。横在他们中间的隔板仍会挡住救援。[可达距离的实现](../../../../../Redflag/src/Redflag/Gameplay/WorldActions.cpp) · [隔板与距离回归](../../../../../tests/NpcWorldTests.h)

同一包还从窗口分别操作了玩家和瓦特的拾取、递药、维修、自疗。十项都核对了动作播放、接触、生效时的世界状态和收手。玩家递药时两人握持点相隔 1.70 厘米；瓦特递药时为 0.39 厘米。每个 `jsonl.gz` 保存一局的输入和连续帧，测量文件中的 `archive_sha256` 可核对发行包。[十项测量](measurements.json)

在仓库根目录运行。瓦特的拾取、维修、递药、自疗均通过通讯栏发送中文指令，复测需要启动本地队友服务。

```powershell
python tests/capture_station_hands.py output/releases/Redflag-Watt-Station-Win64.zip tmp/my-ground-takes
tmp/retarget-env/Scripts/python tests/review_station_hands.py output/releases/Redflag-Watt-Station-Win64.zip tmp/my-ground-takes tmp/my-ground-review --verify
```
