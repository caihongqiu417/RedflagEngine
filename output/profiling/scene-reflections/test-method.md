# 镜中灯牌的像素检查

正交相机面向镜子，灯牌在相机背后。测试先读取相机看到的每个表面，确认整张图的深度和法线都属于镜面。再对着镜中的白条、灰条和黑边读颜色。灯牌最亮处的线性 RGB 是 (4, 0.48, 0.14)。灰条在贴图中的数值是 128，按 sRGB 解码后为 0.21586，因此镜中灰条的红通道应为 4 × 0.21586，实际读到 0.863442。原始读数保存在 [test-pixels.csv](test-pixels.csv)，坐标从图像左上角开始数。最后一列在找到表面时为 1，射线离开场景时为 0。多帧平均的行记录找到表面的帧数占比。

把普通绿色挡板移到镜子和灯牌之间。点光源从侧面照向它时，绿色通道为 0.963723。在灯与挡板之间放一块遮光片，这个位置就变成黑色。拿走遮光片，烘焙旁边白墙反回来的光，绿色升到 1.21776。单独关闭镜中的探针光照，绿色回到原值。另一组用例把镜子转过 60°，沿水平方向平移相机。三个机位都读取镜面上同一个位置，灯牌从完全可见到完全离开画面，镜中仍读到 (2, 0.5, 0.125)。[屏幕颜色与场景遮挡的独立对照](test-screen-priority.csv)

检查镜面抖动时固定相机和像素位置，不开 TAA。连续画 16 帧，将每个像素与第一帧的线性 RGB 相减，取三维向量的长度，再找整组画面中最大的值。镜面粗糙度为零时，旧方法仍给每条光线加了少量随机偏转，灯牌边缘的最大差达到 4.03113。固定反射方向后读到 0。把粗糙度改成 0.55，原本漆黑的灯条外侧又收到了光，16 帧平均的红通道为 1.10144。[连续帧对照和测试程序校验值](test-stability.csv)

在仓库根目录运行下面的命令，可在本机生成全部测量结果并复查截帧。构建为 Debug x64，测试设备为 NVIDIA GeForce RTX 5070 Ti Laptop GPU。普通运行会同时核对环境光和屏幕反射。带 `--scene-reflections` 的运行只截取灯牌计算，模型和其他缓冲区在截帧开始前已准备好。回放脚本读取同一绘制事件的浮点颜色，逐字节与原程序读回的 9,216 个像素比较。[RenderDoc 回放记录](test-capture.json)

```powershell
tests/run_environment_lighting.cmd tmp/scene-reflections/final
$tools = (Resolve-Path tmp/tools/renderdoc/RenderDoc_1.47_64).Path
tests/run_environment_lighting.cmd output/captures/local/scene-reflection-test-ideal "$tools/renderdoc.dll" --scene-reflections
& "$tools/qrenderdoc.exe" --python tests/replay_scene_reflection_check.py
```
