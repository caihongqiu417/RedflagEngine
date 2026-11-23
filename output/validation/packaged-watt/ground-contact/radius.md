# 调整半径后的墙边距离

从发行包中复制两份同样的货场，在出生点正前方 2.50 米放置一面高墙。玩家面向同一个方向，两次都按住 W 走到停下。第一次把玩家胶囊的半径设为 0.20 米，角色原点最后距墙 0.181 米。第二次只把半径加到 0.52 米，距离随之增加到 0.468 米。[两次行走的坐标和发行包校验值](radius.json)

站立和倒地时的移动都使用 `GameWorld.TraverseWithCollider`。半径取自玩家身上的碰撞体，移动时沿用引擎已有的近似，在胸口和头部各扫一个球，球半径取角色半径的九成。上面量到的停车距离与这个数值接近。物理射线和 Forge 里画出的轮廓使用完整胶囊。[角色移动](../../../../SandboxProject/Source/StationMotion.cs) · [移动时的碰撞检查](../../../../Redflag/src/Redflag/Gameplay/TerrainTraversal.cpp)

在仓库根目录运行 `python tests/check_station_radius.py output/releases/Redflag-Watt-Station-Win64.zip tmp/wall-distance.json` 可以重测。脚本将行走速度设为每秒 0.9 米以降低最后一步的误差，并记录每帧的位置。[实机测量脚本](../../../../tests/check_station_radius.py)
