# 源码读本配图

## 场景视口与鼠标坐标

左图是 Redflag 在 Sponza 中庭实际绘制的底色。用 RenderDoc 1.47 打开 [Vulkan 编辑器第 10 帧](../../../../output/captures/local/Forge-Sponza-Lit-10.rdc)，选中第 638 个事件（场景的最后一次索引绘制）。颜色输出 0 是 `ResourceId::3981`，格式为 RGBA8 UNORM，原尺寸为 1393 × 878 像素。通过 `ReplayController.SaveTexture` 导出后保留原始 RGB 三通道，省去 Alpha 以免 PDF 阅读器把叶片混到白纸上。结果为 [sponza-picking.png](sponza-picking.png)。本地捕获文件未随 Git 分发，[帧记录](../../../../output/rhi/editor-sponza-events.json)列出了场景、复现步骤和散列值。图中的模型为 [Crytek Sponza 的 glTF 示例](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Sponza)，作者和许可见上游模型说明。

再读颜色输出 1（`ResourceId::3985`，R32 SINT）。以左上角为原点，下面给出同一位置在两张图里的原始读数。整张编号图有 1,209,167 个像素写着 4，其余 13,887 个像素是背景值 −1。图上的标记由 [picking.tex](picking.tex) 叠加，像素中心在各坐标上加 0.5。

| 标记 | 像素 (x, y) | RGBA8 | 实体编号 |
| --- | --- | --- | --- |
| A，蓝色挂毯 | (410, 205) | (29, 47, 87, 255) | 4 |
| B，石柱 | (365, 760) | (123, 116, 107, 255) | 4 |
| C，屋顶开口 | (690, 40) | (25, 25, 25, 255) | −1 |

PNG 的 SHA-256 为 `46e2fe433fb997cf47c43e7e3cc20fe49bb8d82f11798c707630ed7b97d6b985`。调用 `GetTextureData` 取得的整数图原始字节 SHA-256 为 `964545cfe6fbcf2b43e471a3b1c73e33f63449d20ba5ed2fa550e9fc4188627b`。

## 共享贴图与像素存储

[pixel-rows.tex](pixel-rows.tex) 是四列三行 RGBA32F 小图的原创示意。字节数按正文公式计算。颜色标记 A、B 两个相邻像素。图中排列适用于读回给 CPU 的紧密数据，GPU 上的图像布局由驱动选择。接口见 [ComputeDevice.h](../../../../Redflag/src/Redflag/Renderer/RHI/ComputeDevice.h)。

## 石柱与玻璃

神庙的两张表面图来自实际 Vulkan 绘制。在 RenderDoc 1.47 中定位第 12 帧事件 262，读取两个浮点颜色输出，再分别按 sRGB 底色和映射到 0 至 1 的法线显示。原始模型由 Epic Games 创作，衍生图按 CC BY-NC-SA 4.0 提供。流程图按项目的着色器和光照代码绘制。[像素、事件和来源](../../../../output/scenes/sun-temple/README.md)

玻璃与墙条是项目自建的平面与立方体。两张局部图均从正式编辑器第 12 帧事件 189 的显示输出截取。两次只有折射率不同。[完整图片、裁剪坐标与复查方法](../../../../output/scenes/glass-studio/README.md)
