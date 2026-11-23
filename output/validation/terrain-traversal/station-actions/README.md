# 台阶、沟与坡面

四张图都来自 Release 版的 Vulkan 游戏窗口，大小均为 1920 × 1080。测试把维修站的玩家模型放在单独搭建的平台上，由脚本沿同一条直线向前走。图中只取动作的一个位置，测试脚本把片段停在 0.23 秒供截图。[台阶](step.png) · [越沟](gap.png) · [攀爬](climb.png) · [下坡](slope.png)

台阶高 20 厘米，沟宽 95 厘米，攀爬平台高 1.42 米。第四张图采用一幅 16 位高度图，每走 50 厘米，地面先升高 20 厘米，过顶后按相同坡度下降。记录的 75 个位置都进入了坡面移动状态。角色模型的原点比地面高 7 厘米，将它扣除后，脚下的地形高度与预设坡面一致，按记录精度最大相差 0.01 毫米。[逐帧位置与宿主版本](play.json)

独立的 PhysX 测试还让角色在低梁前停下、中止攀爬，并在撤走落脚点后重新检查能否跳过去。脚踝和手腕的位置用同一个维修站模型求解，分别检查它们能不能到达给定目标。[12 项原生检查](physics.txt)

玩家落地后的脚步另在维修站脚本的测试副本中验证。按空格起跳，进入落地动作后按住 W，角色继续走了 5.41 米。在连续两个完整步态周期里，左右脚都分别领先过另一只脚，两只脚踝每轮都升高约 21 厘米。记录中的足部位置来自混合和贴地处理后的骨架。[输入、剪辑与足部坐标](../../watt-station/melee/landing-walk.json)

在仓库根目录依次运行下面三个测试。

```powershell
tests/run_scene_tests.cmd --traversal
python tests/check_traversal_play.py --exe bin/Release-windows-x86_64/RedflagForge/RedflagForge.exe --output output/validation/terrain-traversal/station-actions
python tests/check_watt_station.py --melee movement --exe bin/Release-windows-x86_64/RedflagForge/RedflagForge.exe
```
