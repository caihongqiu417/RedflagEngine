# 浴室的纹理与法线

图像来自 RTX 5070 Ti Laptop GPU 上正式 Vulkan 编辑器的两次第 12 帧回放。两图取相同的 1391 × 892 显示图，裁取左上坐标 (290,150)、尺寸 900 × 550 的区域。在 RedflagForge 的项目面板打开 `RTXGI-Bathroom.redflag` 可以找到保存的相机。

| 图片 | RenderDoc 中的显示图 | 本机原图 |
| --- | --- | --- |
| [修正前](before.png) | ResourceId 1121 | `tmp/rtxgi-bathroom-capture/panel-1121.png` |
| [修正后](after.png) | ResourceId 1193 | `tmp/rtxgi-bathroom-capture/panel-1193.png` |

两项数字是输出图像在各自捕获中的资源编号。场景由 cenobi 创作，NVIDIA 在 RTXGI 示例中发布，模型与这组衍生图附作者署名，使用 CC BY 3.0。[模型出处与使用许可](https://github.com/NVIDIA-RTX/RTXGI-Assets/blob/6c3caa658d419a00866fcf6b387b0140c1950e17/Bathroom/BLENDSWAP_LICENSE.txt)
