# 瓦特跟随领队跨沟与攀台

蓝色角色是领队，橙色角色是瓦特。两个测试都在正式 Vulkan 游戏窗口运行，使用维修站的角色模型、跟随行为和动作表。平台单独搭建，方便固定沟宽和台高。开场时领队站在远端，瓦特凭角色定义中的默认跟随意图走近；测试只给领队按键。[跨沟实拍](gap.png) · [攀台实拍](climb.png)

沟宽 0.95 米。瓦特从地面上方 0.07 米起步，跨越时实体原点最高到 1.196 米，再落回另一侧的 0.07 米，接着走到距领队 2 米的位置停下。另一条路线的平台高 1.42 米，瓦特攀上后实体原点在 1.49 米处。领队随后沿平台继续走，两次测试中瓦特都再次起步，在领队身后站定。[跨沟逐帧坐标](gap.csv) · [攀台逐帧坐标](climb.csv)

记录中的“跟随”是当前任务。跨沟时，原生移动代码选择跳跃，动作表播放 `jump_1_up`。攀台时选择攀爬，动作表以 `Station-Pickup` 为基础姿势，运行时将手腕引向平台边沿。瓦特落地后直接恢复行走，当前跟随路径没有单独播放玩家使用的落地剪辑。两份 CSV 都保留了瓦特实体、左腕、左脚和领队的世界坐标。[行为变化和程序版本](gap.json) · [高台行为记录](climb.json) · [跟随实现](../../../../Redflag/src/Redflag/AI/NpcSystem.cpp) · [动作表](../../../../RedflagForge/assets/ai/watt.animator.yaml)

在仓库根目录运行两个用例。

```powershell
python tests/check_watt_station.py --watt-terrain gap --exe bin/Release-windows-x86_64/RedflagForge/RedflagForge.exe
python tests/check_watt_station.py --watt-terrain climb --exe bin/Release-windows-x86_64/RedflagForge/RedflagForge.exe
```
