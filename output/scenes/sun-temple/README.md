# Sun Temple

Epic Games 制作了这座神庙，Kai-Hwa Yao 与 Nicholas Hull 将它从 Unreal Engine 导出。这里从 NVIDIA ORCA 的 v4 发布包读取 FBX 和 DDS。模型、纹理与本页衍生的渲染图按 CC BY-NC-SA 4.0 使用。[NVIDIA ORCA 来源](https://developer.nvidia.com/ue4-sun-temple) · [许可证](https://creativecommons.org/licenses/by-nc-sa/4.0/)

准备 Python 3.11 以上版本、Pillow、NumPy 和 Blender 5。从仓库根目录运行 `python tests/fetch_sun_temple.py`。Blender 未加入 PATH 时可传 `--blender "安装目录/blender.exe"`。也可传 `--archive "下载目录/SunTemple_v4.zip"` 使用已下载的原始压缩包。脚本检查 SHA-256，将本地资源放入 `RedflagForge/assets/models/reference/SunTemple`。模型与压缩包均留在 Git 之外。

打开 Vulkan 编辑器，在项目面板双击 `assets/scenes/Sun-Temple.redflag`。主相机沿用 FBX 中的入口机位，朝向神庙内部。选择“前厅灯”降低强度，瓷砖前段会随之变暗；选择“廊灯”可以改变长廊较冷的那束光。该场景保存了 Redflag 的八盏点光源和 Lit 设置。

准备资源时把 517 件静态物体按材质合并，所得 48 组网格合计 606,376 个三角形。DDS 纹理缩至最长边 1024 像素，BC5 法线图恢复 Z 并翻转 Y。原文件的 Specular 贴图把遮蔽、粗糙度、金属度分别放在 R、G、B，脚本在 glTF 中保留了这个排列。148 张图被材质实际引用；两张未被源材质引用的自发光贴图未加入材质。作者的 Falcor 配置另有环境图与发光倍率，截图采用 Redflag 的布光。

三幅实测图均从第 12 帧取得。[最终光照](SunTemple.png) 来自显示转换后的场景输出，[材质底色](albedo.png) 在导出时转为 sRGB，[表面法线](normal-depth.png) 将三个方向分量从 −1…1 映射到 0…1。事件、像素与散列值见 [capture.json](capture.json)。在本机用 RenderDoc 1.47 打开[原始帧](../../captures/local/SunTemple-12.rdc)，事件 262 对应 48 次网格提交的最后一次，事件 274 把几何信息整理给光照计算，事件 358 留下最终画面。
