# 主角落地的动画事件

在 Forge 角色创作面板打开「动画事件」，选择 `land`。[面板截图](forge-events.png) 中保留了原来的屈膝时刻，Recovery 的结束时间已通过键盘从 0.20833333 改成 0.30 秒。保存后，面板重新读取了临时关卡的动作表。测试只编辑副本。

同一款 Operator 模型由两名 C# 角色使用。第一名完整播放了落地剪辑，脚本依次读到 Recovery 开始、KneeBend 时刻和 Recovery 结束。第二名在第零秒切到待机，当即收到带 Interrupted 标记的结束。对比修改前后的记录，屈膝仍在 0.08333333 秒，第一个角色的结束时刻从 0.208333328 变为 0.3 秒。[原始动作表的 C# 回执](default-notices.json) · [面板保存后的 C# 回执](edited-notices.json)

再在正式维修站按空格，实际主角由物理落地触发剪辑。测试脚本每帧观察主角的恢复状态。在这次 Debug 运行中，显示状态持续了约 0.397 秒及 0.550 秒。关卡每帧执行脚本一次，帧耗时大于这段动画的窗口。因此这两次操作证明了主角能开启与结束恢复状态。需要核对配置的精确时刻时，应读上面两份事件回执。[原版主角](default-player.json) · [改过结束时间的主角](edited-player.json)

## 复测

从仓库根目录运行，下列命令需要已编译的 Debug 引擎、测试用 Python 环境，以及角色创作工具的依赖。最后两条把上一步保存的动作表交给游戏。

```powershell
tests/run_scene_tests.cmd --animation-events
tests/run_behavior_tree.cmd --events-image output/validation/animation-events/forge-events.png
python tests/check_watt_station.py --landing --name default-player
python tests/check_watt_station.py --animation-actors --name default-notices
python tests/check_watt_station.py --landing --animator tmp/npc-authoring-panel/watt.animator.yaml --name edited-player
python tests/check_watt_station.py --animation-actors --animator tmp/npc-authoring-panel/watt.animator.yaml --name edited-notices
```

[原生时间线测试](timeline.txt)包含单帧跨过两个循环、跨越起止端点、同一通知只读一次、切换动作和两名角色分别读取消息。

## 保存前看两帧姿势

在 Recovery 的「结束（秒）」填 0.30，点「查看结束姿势」。左侧用磁盘动作表的 0.2083 秒，右侧用编辑器草稿的 0.30 秒。面板调用实际 Operator GLB 的动作采样，用同一比例绘出关节。[修改前后的姿势对照](forge-pose.png)

初版连上模型列出的全部 145 个关节。身体以外的 IK 控制器把预览的显示范围撑大了。现在根据导入后真正带动顶点的权重选关节，将它们连接为 99 段骨头。两次采样中位移最大的骨段末端相差 0.350447 米。测试同时操作了界面按钮，核对了实际产生的两幅图，再保存、重读原动作表。

从仓库根目录运行 `tests/run_behavior_tree.cmd --pose-image output/validation/animation-events/forge-pose.png`。这项测试使用同一份 Forge 窗口代码和 Dear ImGui 的 DX11 测试后端。游戏中的事件回执另由上面的 Vulkan Play 验证。
