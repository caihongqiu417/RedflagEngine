# 角色的手与场景物品

医疗包有两个位置值得分清。包落在地上时，模型原点在正面的十字中央，把手比它高 7.3 厘米。玩家按 E 后，脚本先朝包走近，播放弯腰动作。GLB 的这段动作在第 36 帧把左手伸到脚侧，角色因此会略微侧身。把手落入掌中的那一帧，背包数量才增加；地上的模型在同一位置换成跟随手的模型，角色收手时再把包收到腰侧。原来那只红工具箱一直留在地上。[场景和挂点](../RedflagForge/assets/scenes/Watt-Station.redflag) · [玩家交互脚本](../SandboxProject/Source/StationInteraction.cs)

## 握持位置与站位

`ContactSocketsComponent` 的每个名字代表模型上的一点。`Point` 的三个数字依次是局部 X、Y、Z，单位为米。省略 `Bone` 时，这一点跟着整个物件移动。发电机的 `repair` 点就取自作者模型正面的拨动开关。指定 `Bone` 后，点会跟着那根骨头。例如两个机器人都用左手中指第一节 `f_middle.01.L` 的原点作为握持点；救援目标则使用对方的左腕。胸板跟着胸骨转动，受骨骼缩放影响，从胸板外表面到药包中心的实际距离约等于包的半厚 4 厘米。[场景字段](../Redflag/src/Redflag/Gameplay/GameplayComponents.h) · [药包资产](../RedflagForge/assets/game/station/crew/Medkit.glb)

换一双手，可以在场景里一起调整两个数值。`GiveDistance` 是交接时两人脚下相隔的距离，维修员用 0.87 米，两只手接近药盒时手肘还有弯曲的余地。`GripTolerance` 是手与目标能相差的最大距离，这个药盒厚约 8 厘米，因此每一方的握持范围取 4 厘米。录像里测到的实际距离另外记录。

站在发电机背面时，光让手臂弯曲会让手穿过机器。目标可设置 `Stand`，表示角色应该站在开关的哪一侧，向量相对目标模型。引擎将设备的碰撞盒向外扩大角色半径，比较沿两侧经过拐角的距离，再通过现有地面移动接口走到指定位置。对于地上的小医疗包，可以省略 `Stand`。引擎读取拾取片段在接触时刻的握持位置，以原动作的伸手方向和距离决定站位。这段接近只规划绕过目标自己的碰撞盒，其他墙体仍由现有移动和遮挡查询处理。[站位和手臂求解](../Redflag/src/Redflag/Gameplay/HandContact.cpp)

## 播放与生效

玩家和瓦特共用 `ApproachContact`、`BeginContact`、`AtContact` 和 `EndContact`。第一步让角色走到合适的位置，第二步开始伸手。原片先带动肩和肘，接近触点时才逐步用逆向运动学微调手臂。递药时，交付者和接收者都要够到两人之间的交接点。该点随两人的实体位置变化，收件人走远后库存检查仍会失败。[C# 接口](../Redflag-ScriptCore/Source/Scene/GameWorld.cs) · [瓦特的动作执行](../Redflag/src/Redflag/AI/NpcSystem.cpp)

动作通知表示片段已经播到可以交接的位置。`AtContact` 再用当前画面中手指的世界坐标量一次距离。场景还会复查目标实体、原有距离和遮挡，全部通过才转交物品或恢复生命。发生交接时，接收者拿到此前在交付者手中的同一个道具实体。松开 E 可中断维修和救援，下一次从头计时。尚未伸手的拾取可用方向键取消。没有配备动作挂点的旧宿主继续使用原来的物理交互接口。[动作和库存](../Redflag/src/Redflag/Gameplay/WorldActions.cpp)

## 实机核对

在仓库根目录运行 `python tests/check_watt_station.py --interaction pickup`，将最后的名称换成 `repair` 和 `revive` 可分别检查另外两种操作。测试打开真实的原生编辑器和角色模型，只调整初始位置、镜头、生命和场景中的干扰物。每个操作都通过正常按键触发。玩家修理中松开 E 时设备电量仍为 0；再次按住才恢复到 100。扶起实验中瓦特的生命从 0 回到 50，并开始播放从地上站起来的动作。[拾取和用药](../output/validation/watt-station/interactions/pickup.json) · [修理](../output/validation/watt-station/interactions/repair.json) · [救援](../output/validation/watt-station/interactions/revive.json)

Harness 逐帧保存 `contact.hand` 和 `contact.goal`，两者都是米制世界坐标。`commit_distance` 只在物品或生命确实改变后出现，是交接时这两个点的距离。规则允许最多 4 厘米。测试中读到小于一微米，只说明配置的点在浮点计算精度内对齐；角色手指表面和模型细部的贴合还需结合实拍检查。[帧记录实现](../Redflag/src/Redflag/Debug/PlaytestSession.cpp)

旧货场的一次完整试玩里，瓦特停在世界坐标 `(10.2072849, 0.08, 1.22944653)`，发电机在 `(12.1999998, 0.08, 1.39999998)`。把两点相减再求长度，按引擎的单精度运算得到 2.000000238 米，恰好还在允许的 2 米外。旧代码只把目标距离缩短一个浮点数间隔，加回十米左右的位置时，这一小步被舍掉了。现在最后一步会走进范围一厘米，随后照常检查遮挡、绕到面板和实际握持。可在根目录运行 `tests/run_scene_tests.cmd --station-reach`；这项独立原生测试复用实机坐标、NPC 行走、真实交互计时，不加载角色模型。[原生回归](../tests/NpcWorldTests.h) · [实测记录](../output/validation/station-actions/station-work-distance.txt)

## 货场中的跟随与遮挡

瓦特离开发电机时，站在它和玩家之间的巡检机会转身朝向它。原来的跟随始终沿着两人连线迈步，碰撞停下来后，下一帧还会向同一个方向走。现在瓦特从身体中央和两侧查看前方，遇见其他角色或爬不上去的高箱子，会比较沿碰撞盒两侧的路程，再朝选定的箱角走。走到拐角后重新看向玩家。这个办法适合绕开近处的一两个物体，连续房间之间的寻路仍需另外设计。小台阶、沟和能攀上的平台继续使用原来的地形动作。[跟随移动](../Redflag/src/Redflag/AI/NpcSystem.cpp) · [碰撞盒与地形移动](../Redflag/src/Redflag/Gameplay/TerrainTraversal.cpp)

原生测试将三人放回上次被堵住的位置，并让巡检机始终转向瓦特。瓦特沿地面绕出，最终距离领队 1.99 米，角色基点最高仍为 0.08 米。可在仓库根目录运行 `tests/run_scene_tests.cmd --station-reach`。这项测试单独检查 PhysX 和跟随动作。完整货场里的敌人巡逻、追逐和战斗另由正式游戏试玩核对。[原生测试输出](../output/validation/station-actions/station-work-distance.txt)

巡检机通过观察窗寻找玩家上身，放电时脉冲会打向较低的位置。两个点的高度不同，玩家露出头部时，面前的半高箱仍可能挡住脉冲。现在沿画在场景里的脉冲方向查询第一个碰撞体，把脉冲画到碰撞点，只在碰到目标时扣除生命。运行 `python tests/check_watt_station.py --sentinel-cover` 会打开使用正式脚本的隔板测试场景。1.275 米高的板能截住脉冲。升高到 1.93 米后巡检机丢失玩家，取消蓄能，搜索后回到巡逻。放低板以后，巡检机重新蓄满 0.8 秒才击中目标，玩家生命由 100 变为 88。途中若巡检机本身损坏，则立即停止蓄能。[巡检机脚本](../SandboxProject/Source/StationGuard.cs) · [动作和生命记录](../output/validation/station-actions/sentinel-cover.json) · [巡逻步态记录](../output/validation/station-actions/sentinel-yard.json)
