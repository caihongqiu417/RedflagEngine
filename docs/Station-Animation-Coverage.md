# 维修站的角色动作

玩家和瓦特共用维修员模型，巡检机使用另一套骨架。下面从游戏里能做的事情查到对应剪辑。「实机」表示已经在启动的游戏中取得画面、剪辑名或世界状态；旁边注明具体取得了哪一种。「模型」表示将正式 GLB 交给引擎播放器采样。比如能读出射击时右腕移动了多少，但单凭这个数字并不能判断脉冲有没有击中目标。

## 玩家

| 操作 | 身体动作与恢复 | 已核实的结果 |
| --- | --- | --- |
| 行走、跑步、转身 | 徒手和持炮各有一组走跑动作。与位移相配的步态按角色实际走出的距离推进，松键后回到待机。 | 实机在转动相机后分别按前、后、左、右，并检查四个世界方向与持炮行走剪辑。模型核对了完整左右步幅。[移动与动作记录](../output/validation/watt-station/melee/movement.json) |
| 蹲下、蹲行 | 原地播放 `Station-Crouch`，移动改播 `Station-CrouchWalk`，松开 Ctrl 恢复站姿。 | 实机读出了蹲姿和蹲行。模型以 60 Hz 检查靴底，蹲行一个循环是 2 秒、1.453 米。[实机姿势](../output/validation/watt-station/melee/movement.json) · [靴底实测](../output/validation/station-actions/model.txt) |
| 出拳 | F 起手，左拳在 0.20 秒抵达最远处，收拳后恢复。倒地会打断出拳。 | 实机分别验证命中、距离不够、被墙挡住和倒地中断。完整剪辑长 0.867 秒，模型中左腕前移 0.551 米。[命中](../output/validation/watt-station/melee/hit.json) · [挥空](../output/validation/watt-station/melee/miss.json) · [隔墙](../output/validation/watt-station/melee/wall.json) · [中断](../output/validation/watt-station/melee/cancel.json) |
| 换装、瞄准 | 1 和 2 在臂炮与扫描器之间切换，两次都播放同一段装备动作的前 0.35 秒。静止持炮按右键显示单帧瞄准姿势 `AIM-Center`，松开回到待机。 | 实机记录到换装后两次回到待机、随后举炮瞄准。模型中瞄准时的右手与待机相差 0.578 米。这一帧由 `GameWorld.PlayClip` 显示；用需要正时长的 `Animator.Play("aim")` 无法启动时间轴。[装备实机记录](../output/validation/watt-station/interactions/equipment.json) |
| 发射臂炮、使用扫描器 | 左键发射从右手位置出发的脉冲，播放 `Cannon_Charge` 的前 0.22 秒。切到扫描器后，左键沿相机方向查找物体，身体保留当时姿势。 | 实机里扫描得到巡检机 1103，生命值不变；切回臂炮后命中同一目标，生命从 75 降到 50。模型在开火这 0.22 秒内右腕移动了 0.321 米。[装备实机记录](../output/validation/watt-station/interactions/equipment.json) |
| 起跳、滞空、落地 | 起跳与滞空分别播放 `jump_1_up`、`jump_3_midair-cycle`。落地剪辑的缓冲区间从 0 秒到 0.208 秒，骨盆在约 0.083 秒时屈膝。 | 越沟有正式窗口画面；落地有默认动作表的脚本通知，模型另测了三段骨骼。画面和事件来自不同跑次。[越沟与平台](../output/validation/terrain-traversal/README.md) · [落地通知](../output/validation/animation-events/default-notices.json) |
| 攀爬 | 基础姿势复用 `Station-Pickup` 的弯腰动作，两只手在攀爬期间向平台边缘靠近。 | 独立地形场景在实际游戏窗口爬上 1.42 米高的平台。额外测了移动平台被撤走时取消攀爬。[地形实测与截图](../output/validation/terrain-traversal/README.md) |
| 拾取、使用医疗包 | 对准可拿物品按 E，伸手到 1.20 秒时再查距离并改库存，收手后恢复待机。按 H 直接用药，当前版本没有专门的用药动作。 | 实机记录到触点前背包仍有 1 份药，触点后有 2 份，场景中的物品消失，收手后为待机。按 H 治疗从 40 到 90，持续按住只用一份。[玩家拾取与用药](../output/validation/watt-station/interactions/pickup.json) |
| 维修、扶起瓦特 | 按住 E 维修时循环 `Cannon_Charge-cycle`；扶起时播放 `Station-Pickup`。两者复用已有表演。离开、松手或交互完成后，玩家恢复常规动作。 | 实机松开维修键后恢复待机，发电机仍未修好；重新按住完成维修，电力从 0 到 100。救援使瓦特的生命从 0 到 50。现有记录没有验证手掌与工具、队友身体的逐帧接触。[维修中断与完成](../output/validation/watt-station/interactions/repair.json) · [扶起瓦特](../output/validation/watt-station/interactions/revive.json) |
| 受伤、倒地 | 普通扣血由游戏规则处理。生命降为零后，玩家脚本持续选择重受击剪辑 `flinch_heavy`，等待救援或失败。 | 实机有倒地打断近战与关卡失败记录。模型检查了整段 0.417 秒的重受击动作。普通扣血没有独立动作，现有证据也没有一段持续倒卧的专用剪辑。[中断回执](../output/validation/watt-station/melee/cancel.json) · [模型采样](../output/validation/station-actions/model.txt) |

## 瓦特与巡检机

| 场景中的动作 | 实际剪辑与验收 |
| --- | --- |
| 瓦特跟随、待命 | 跟随时朝目标转身并播放 `walking_nogun-cycle`，进入距离范围或待命后回到 `Idle-cycle`。前往玩家送药和原地等玩家来取，实测瓦特的位移分别为 3.830 米和 0 米。[队友指令与位移](../output/validation/station-companion/README.md) |
| 瓦特拾取、递物 | 靠近并转向后，`Station-Pickup` 的接触时刻为 1.20 秒，`Station-Give` 为 0.733 秒。实机让瓦特空手起步，左腕在两段中分别移动 0.675 米和 0.641 米；第 38 帧瓦特背包从 0 变 1，第 59 帧玩家从 1 变 2。若瓦特一开始已有药，模型也可能直接交付背包中的药。发行包另验了这个流程。[拾取姿势](../output/validation/station-actions/gather-and-give/pickup-pose.png) · [手腕与双方库存](../output/validation/station-actions/gather-and-give/pose.json) · [包内交付](../output/validation/packaged-watt/animation/delivery/delivery.png) |
| 瓦特维修、扶起玩家 | 第一局由瓦特恢复供电，两人一起撤离。重新开局后，瓦特两次扶起被巡检机击倒的玩家。另在正式游戏程序的测试场景中，瓦特在救援阶段播放 `Station-Pickup`，9 个采样帧中左腕最大位移为 0.568 米；玩家生命从 0 到 50 后，瓦特恢复跟随。记录可以证明实际播放和恢复，仍未测手掌与身体表面相距多少。[通关记录](../output/validation/packaged-watt/animation/mission/mission.json) · [两次救援的逐帧记录](../output/validation/packaged-watt/animation/mission/frames.jsonl) · [救援手腕逐帧数据](../output/validation/station-actions/sentinel.json) |
| 瓦特跳跃、攀爬及自身倒下 | 实机用两条独立地形路线触发瓦特自动跨 0.95 米沟和攀 1.42 米平台。阶段由走路变为跳跃或攀爬，再恢复行走、停住；领队继续走时，瓦特重新起步。越沟用 `jump_1_up`，攀爬复用弯腰动作并把手腕引向台边，落地直接接行走。瓦特自己倒下时停止执行命令，当前没有为它播放专门的倒卧剪辑。[跟随路线、关节数据与截图](../output/validation/terrain-traversal/watt-follow/README.md) |
| 巡检机追赶、近身放电 | `Walk-cycle` 的根骨名为 `MASTER`，原片段在 2.5 秒里向前走 2 米。游戏移动实体后固定这条根骨，双脚仍然按步态运动。正式游戏程序一次连续追赶中，实体前进 2.670 米，跨过一个循环，8 个行走采样帧里根骨与实体的水平差均为 0；左后脚上下相差 0.435 米。近身后切到 `Cannon_Fire_Recoil`，玩家受到放电伤害。[实体与骨骼逐帧记录](../output/validation/station-actions/sentinel.json) |
| 巡检机损坏 | 以上述近身放电为起点，由测试脚本把巡检机剩余生命扣完。正式巡检机脚本切换到 `Damage_heavy` 并停在 0.30 秒。接下来 9 个采样帧中生命为零，机身节点保持原位置。[损坏前后的逐帧记录](../output/validation/station-actions/sentinel.json) |

## 跑步抬脚时的鞋底

要分清鞋尖向后勾与整只脚向一侧翻转，单看踝骨高度不够。模型检查选取至少 80% 蒙皮权重来自同侧脚骨、脚盖和脚趾的顶点，在待机时从靴底后跟和前掌各选一块朝下的三角形。记录网格名称和顶点编号后，就能逐帧算出同一块鞋底朝哪里。下面的倾角是它与待机时鞋底方向的夹角。绕脚长轴的侧转另行计算。这里把鞋部最低顶点离平地不到 3 厘米的采样记为近地，余下记为摆动。3 厘米是本次用于分组的距离。

| 剪辑 | 近地时有无翻底 | 摆动时四块面片中最大的倾角 | 观察 |
| --- | --- | ---: | --- |
| 普通行走（含持炮） | 无 | 30° | 两脚轮流支撑。鞋底最低处最多低于平地约 1.1 厘米。 |
| 奔跑（含持炮） | 无 | 133° | 向后提靴时脚底确实会朝上。此时整只靴子的最低点已抬高约 0.655 米，绕脚长轴的扭转最多约 40°。原片段中也有约 1.8 厘米的轻微穿地。 |
| 蹲行 | 无 | 50° | 两脚的前掌、后跟均未出现朝上的采样。 |

逐帧使用每秒 120 个样点。Godot 原模型与导入后的 Operator 在徒手、持炮走跑四段的结果逐项相同。这里测的是原剪辑在平地上的姿势；角色跑到工具箱等场景物件旁，地形 IK 还会调整膝、踝，最终显示的脚尖高度可能不同。仅凭原剪辑的 0.655 米不能解释实机中更高的脚尖位置。[实际鞋底三角形和分项数据](../output/validation/station-actions/soles.json) · [Godot 原件对照](../output/validation/station-actions/soles-godot.json) · [检测程序](../tests/check_station_soles.py)

走路有一刻前掌仍然压在地面，脚跟和踝骨却已经抬起来了。在 `walking_gun-cycle` 的 1.017 秒，机器人的左踝比静止时高 20.2 厘米，真实鞋底只比静止时高 1.08 厘米。若把踝骨抬高的 20.2 厘米当成整只靴子已经跨过的高度，前方即使有 20 厘米的台阶，游戏也会认为无需再抬脚。

角色脚下的高度用每只靴子六个点就可以估算。点都取自实际模型，其中一部分随脚掌骨 `foot` 移动，另一部分随脚趾骨 `toe` 移动。选点时分别考虑后跟、外侧边缘及前掌随步态翻转时出现的最低点。以五段走跑、蹲行、弯腰和落地动作来选点，保留能够改进整套动作估计的顶点。最后用全模型 35 段剪辑的 7,636 个姿势复核。与完整蒙皮鞋底相比，相对静止姿势的抬高量最大误差为左靴 6.48 毫米、右靴 5.56 毫米。以上述踮脚帧为例，六点估计和整个靴底到毫米位都为 11 毫米。记录给出了源顶点编号和对应关节的局部坐标，便于换模型时重新选点。[角色接触点](../output/validation/station-actions/sole-contacts.json) · [逐个动作的误差](../output/validation/station-actions/sole-contact-check.json)

## 工具箱旁的脚步

上一轮只检查了角色能否走到目标、动画是否切到行走，以及任务有没有完成。这些记录能证明玩家按 W 后位置变了，却看不出靴底有没有滑过地面。旧版甚至没有给玩家绑定出拳；手腕在其他动作中会转动，现有检查也能通过。现在检查走路时同时记录脚踝、脚尖和地面。检查出拳则从实际按 F 开始，跟踪肩、肘和拳头，最后核对接触时目标有没有扣血。

跑过红色工具箱时，左脚看起来抬得特别高。我们把人物位置从骨骼坐标中减去，再在原模型的同一时刻量一次。两边都在跑，也都应该抬脚，但其中一帧原动作的脚尖仅高 0.68 米，游戏里却到了 1.15 米。连续三帧的左踝恰好经过箱体上方，另一只脚仍与原动作吻合。[出问题的实机连拍](../output/validation/packaged-watt/animation/after/feet.jpg) · [逐帧对照](../output/validation/packaged-watt/animation/after/authored-feet.json)

引擎在脚下向下发出一条射线，查找可以踩的表面。箱子高 54 厘米，旧代码从地面以上 38 厘米处发出射线。起点已经在箱内，PhysX 返回的命中距离为零。把这个起点当作地面会再次抬起膝盖，还会连带改变靴子的朝向。引擎现在忽略这种命中。遇见真正的台阶时，再用随鞋一起运动的接触点判断鞋底还差多高。这样脚跟抬起、前掌仍贴地时，前掌也能参与计算。[地形与脚掌的实现](../Redflag/src/Redflag/Gameplay/TraversalRig.cpp)

## 复查

模型检查以约 30 Hz 取出维修员的 151 个节点和巡检机的 69 个节点，检查变换矩阵中的每个数都是有限值。再逐项测量表演中应当移动的部位，例如手腕、骨盆或炮身。只使用剪辑前段的动作按照游戏实际使用的秒数取样。站立与单帧瞄准都可以保持完全静止，检查这两种姿势时直接比较同一只手的位置。时间轴另外验证拾取、交付、出拳和落地的通知，以及维修循环、救援结束后能重新进入待机。[模型与状态验证](../tests/StationActionClips.h) · [真实测量输出](../output/validation/station-actions/model.txt)

瓦特拾取与交付的这组截图使用第三人称镜头，工作台边沿遮住了部分手指。手腕位移取自本次游玩中蒙皮使用的世界坐标，谁拿到了药可以查同一帧的双方库存。要重拍这组图，启动项目决策服务，运行 `python tests/check_watt_station.py --npc-motion --orbit-yaw -12 --name my-transfer`。每次选择新的目录名即可保留旧的对照。

在仓库根目录运行以下命令。`--sentinel` 用正式 Debug 游戏程序和现场编译的游戏脚本启动追赶测试。巡检机击中玩家后，测试通过游戏的扣血接口制造巡检机损坏、玩家倒地两种情况。瓦特使用角色原有的救援规则。最后一条重算模型鞋底角度，依赖模型导入所用的 NumPy 和 SciPy。

```powershell
tests\run_scene_tests.cmd --npc-animation
tests\run_scene_tests.cmd --physics
tests\run_scene_tests.cmd --animation-events
tests\run_scene_tests.cmd --traversal
python tests/check_watt_station.py --sentinel
tmp\retarget-env\Scripts\python tests/check_station_soles.py
tmp\retarget-env\Scripts\python tests/check_station_contacts.py
```
