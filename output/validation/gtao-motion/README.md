# 柱脚的环境遮蔽

`forge-before` 和 `forge-after` 是正式 Vulkan 游戏窗口的回放。打开同一编号的 `lit-6.png` 看环境光照，`ao-6.png` 看着色器算出的可见度，`post-detail.png` 是它按原像素裁出的立柱局部。编号 0、6、12 对应相机越过箱子正面时的左、中、右位置。游戏目标实际为 1920×1080，测试文件名中的 480 只控制编辑器预览宽度。场景使用项目内的工作室 HDR，没有点光源，材质为同一种粗糙的灰色非金属。正式帧暂时把历史权重设成 0，以便隔离当前帧的 GTAO。连续移动时开启 TAA 的记录另放在 `sweep-after`。

从仓库根目录可以重建测试。第一条用正式库中的 GTAO 和 TAA 画 13 个连续机位。第二条按各机位的相机矩阵，在同一批地面点取样，对每点发出 8192 条余弦加权半球射线作为独立参考。第三条运行正式 Forge 并捕获三个机位。RenderDoc 1.47 安装在 `tmp/tools/renderdoc/RenderDoc_1.47_64`。

```powershell
tests/run_screen_effects.cmd --gtao-camera
python tests/check_gtao_motion.py --output output/validation/gtao-motion/sweep-after --compare output/validation/gtao-motion/sweep-before/metrics.json
python tests/capture_gtao_motion.py --output output/validation/gtao-motion/forge-after
python tests/capture_gtao_motion.py --output output/validation/gtao-motion/forge-after --views 6 --reuse --time
```

射线长度上限和项目一致，均为 0.65 米。在 0.39 米以内给挡住的方向全部权重，随后用相同的平滑曲线减小权重。箱脚测 2–10 厘米内 75 点，柱脚只保留 13 帧里都在地面上的点。三机位的高分辨率画面另沿柱中心半径 12、18、26、34 厘米的圆环各测 192 点。报告记录每个机位实际看见的个数。相邻两点的误差之差可量出柱子周围灰带的变化，柱子前后被遮住的位置不参加比较。天空以蓝灰色标出，模型上的灰度未经曝光和对比度拉伸。

**版本和捕获。** 旧版正式程序 SHA-256 以 `fd5d6530a55f` 开头，新版以 `962800655604` 开头。各帧 JSON 有完整程序和 RDC 摘要、场景画幅、GPU 事件和本地捕获路径。整帧约 150 MiB。另保存了两份小型 Vulkan 捕获，可在相同表面输入上检查旧版与新版计算过程。[旧版](../../profiling/gtao-camera/before.rdc) · [新版](../../profiling/gtao-camera/after.rdc)

**时间和限制。** RTX 5070 Ti Laptop GPU，Windows x64，Debug。计时只读中间机位的 GTAO GPU 事件，先预热一次，再保留 11 次，表中取各次总和的中位数。旧版 1.312 ms，新版含过滤 2.045 ms。两份 1080p 捕获在运行 GTAO 前都用 3 次绘制写入相机表面。CPU 提交时间、整机游戏帧率和其他显卡需另测。屏幕外或被完全挡住的物体无法参与 GTAO。正交镜头的柱脚与参考更接近，但逐帧变化比旧版略大，原始数据如实保留。

可见弧积分参考 Jimenez 等人的第 2 节。半球射线对照和邻域过滤也可对照 Intel 的实现说明。[GTAO 技术报告](https://www.activision.com/cdn/research/PracticalRealtimeStrategiesTRfinal.pdf) · [Intel XeGTAO](https://github.com/GameTechDev/XeGTAO)
