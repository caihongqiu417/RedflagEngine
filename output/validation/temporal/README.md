# Sponza 的静止画面

从仓库根目录运行 `tests\run_screen_effects.cmd`。测试固定相机、曝光、点光、输入种子和内部画幅 161×91，用 GPU 绘制两个周期、每周期 16 个相同的子像素位置，在第二个周期统计相邻帧的亮度变化。以同一组 16 个输入的算术均值衡量细节误差。移动相机、第一帧和手动清空历史另有用例。

[修改前](before.png) · [修改后](after.png) · [16 次采样参考](reference.png) · [旧画面的连续帧差](before-motion.png) · [修正后的连续帧差](after-motion.png) · [数值](metrics.json)

`before.npz` 保存旧版 GPU 实际输出的第 17–32 帧（线性 RGB）和 16 个未混合输入的哈希。`tests/temporal_quality.py` 重算灰度差、误差与 PNG；两张帧差图使用同一曝光并把亮度差放大 8 倍。固定输入和核对条件相同时才比较旧版与新版。
