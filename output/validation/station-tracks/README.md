# 出生点的两道车辙

站在维修站出生点，角色脚下的原版已经使用碎石法线和视差遮蔽，但远近都铺着同一块碎石。现在可以从两道胎印之间看到原来的砂石。轮迹绕过工具车，在棚前变浅。两幅实拍保留相同的日照、曝光、碰撞、角色和相机。只在测试副本移除或保留实体 1280。[原图](tracks-original.png) · [加车辙](tracks-current.png) · [上下对照](comparison.jpg)

车轮中心相距 1.54 米，单轮胎纹宽约 34 厘米。这两项尺寸由关卡选定。泥土颜色、凹凸和粗糙度来自 Amal Kumar 拍摄的 2.25 米宽扫描。用扫描高度找出较低的轮槽，在槽边留下高处露出的碎石。模型上方高出原平面 3 毫米，用于避免两个重合表面在深度测试中交替出现；行走高度仍取原来的碰撞体。地表带是一份普通模型，其他关卡可以用不同宽度、弯道和材质。更换扫描的通用入口见 [地表带用法](../../../docs/Surface-Strips.md)。[Muddy Tracks，Poly Haven，CC0](https://polyhaven.com/a/muddy_tracks) · [原砂石的扫描](https://polyhaven.com/a/gravel_road) · [本次使用的文件和校验值](../../../RedflagForge/assets/game/station/salvage/tracks/sources.json)

Windows x64 Debug，RTX 5070 Ti Laptop GPU，驱动 616.56，RenderDoc 1.47。两次均从正式 `--play` 入口打开维修站，在第 12 帧记录 1920×1080 的游戏输出，相机为 `(-11.5540, 2.1159, 2.0200)` 米。用每次绘制前、绘制后与最后一帧的深度找出最终可见区域。原有碎石覆盖 860,798 个像素，其中 86,064 个在加入车辙后落在轮迹中，约占 10.0%。这些像素的最终 RGB 平均绝对差为 21.51 个 8-bit 色阶，78,396 个像素的 RGB 平均绝对差至少为 8。动态角色仍会随运行时间略微移动，统计只取两帧共同可见的路面。[图像测量与 EXE 校验值](comparison.json)

九个路径点连成 16 段，两个轮胎合计 64 个三角形。原型将同一条曲线分为 128 段，共 512 个三角形；单组普通回放中，开启后的 draw 与 dispatch 合计曾达 68.40 ms。两组采样时间不同，无法用这一次差值判断计算着色花了多少时间检查曲线。正式对照使用 64 个三角形，两份帧按无、有、有、无交替回放，每次预热一次再读五次 GPU 时间。画面增加两次 draw，dispatch 次数持平，写入车辙底色和法线的那次绘制中位数为 14.58 微秒（P10–P90 为 7.89–15.40 微秒）。同一帧中全部 draw 和 dispatch 的总和中位数为 47.17 与 44.81 毫秒，两组 P10–P90 区间分别为 43.42–50.37 与 44.03–50.61 毫秒。范围重叠，尚不能据此判定整帧速度有变化。未单独埋点测 CPU 提交。[交错回放原始样本](tracks-timing.json) · [无车辙](tracks-original.json) · [正式车辙](tracks-current.json) · [128 段原型](tracks-128-segments.json)

从仓库根目录分别运行 `python tests/capture_station_materials.py tracks original --exe 游戏EXE` 和 `python tests/capture_station_materials.py tracks current --exe 同一EXE`，再运行 `python tests/compare_station_tracks.py 同一EXE` 导出图像对照。设置环境变量 `RF_COST_VIEW=tracks`，在仓库根目录用 RenderDoc 的 `qrenderdoc.exe --python tests/replay_station_cost.py` 重做交错计时。原始帧在各 JSON 的 `capture` 字段对应的本机目录。
