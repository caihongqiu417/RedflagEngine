# 方向光照在两面墙上

在正交相机前放两排相同的墙面像素，上排在 3 个场景单位外，下排在 11 个单位外。太阳从相机背后垂直照向墙面。两排墙朝向相同，因此每平方米应收到同样多的阳光。相机背后的挡板只遮住画面的左半边。打开太阳的阴影后，左半边应变黑，右半边应保持原来的亮度。测试直接给 Vulkan 着色器提供墙面的世界位置和材质，挡板使用引擎的立方体。

在这个例子里，墙面的粗糙度为 1、金属度为 0，太阳正对墙面，相机也正对墙面。此时每个颜色通道的输出可以简化为 `入射光 × (0.96 × 墙面底色 + 0.01) / π`。红色入射光为 2，墙面底色的红通道为 0.8，得到 0.495290。这里读的是曝光处理前的线性 RGB。

旧库（提交 `f589018`）收到新接口传入的太阳方向后，仍把它当作位置计算距离衰减。右侧近墙的红色为 0.0246442，远墙只有 0.00334322。第一次比较就失败，结果见 [before.csv](before.csv)。修改后，逐灯、分簇和单灯 ReSTIR DI 都得到 0.495290。所有 16 组全图比较中，最大通道误差约为 0.000000224，出现在太阳反转后的雾里。[完整读数](directional.csv)

雾的比较用黑色底图。左边处于阴影中时，雾不会把太阳光散射进镜头。右边的光路越长，镜头收到的散射光越多，因此近墙前的红色是 0.0137484，远墙前是 0.0313781。镜面另测了一个方向。把两排墙换成平面镜，镜中只能看到相机背后的挡板。太阳照亮挡板面向镜子的一面时，左半幅能在镜中看到它，右半幅的同一条反射光线会穿过空处。

## 截帧与复现

Windows、Debug x64、Vulkan、NVIDIA GeForce RTX 5070 Ti Laptop GPU，画幅 48 × 16。测试使用 [DirectionalLightCheck.h](../../../tests/DirectionalLightCheck.h) 中的 `CheckDirectionalLight`。在仓库根目录运行 `tests\run_screen_effects.cmd --directional`。

用 RenderDoc 1.47 打开[实测帧](DirectionalLight_capture.rdc)。事件 8 计算所有灯，22 与 33 筛选灯和绘制分簇光照，46 与 58 为 ReSTIR DI 选择并复用灯，71 计算雾。回放脚本逐像素核对了四次颜色输出，还在近墙和远墙各取两个点，与运行时的读数比较。捕获过程中的验证消息为空。[回放记录](replay.json)

以下 PowerShell 命令重新捕获同一场景，再回放它。第二行使测试使用刚指定的 RenderDoc 版本。如果安装在其他位置，把第一行换成当地的安装目录。

```powershell
$rd = (Resolve-Path tmp/tools/renderdoc/RenderDoc_1.47_64).Path
$env:VK_ADD_LAYER_PATH = $rd
$env:VK_INSTANCE_LAYERS = 'VK_LAYER_RENDERDOC_Capture'
./tests/run_screen_effects.cmd --directional "$rd/renderdoc.dll"
Remove-Item Env:VK_INSTANCE_LAYERS, Env:VK_ADD_LAYER_PATH
Start-Process "$rd/qrenderdoc.exe" -ArgumentList '--python tests/replay_directional_light.py' -WindowStyle Hidden -Wait
```
