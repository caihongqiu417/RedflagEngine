# 编辑器手册的图

原图来自 Windows 下运行的 RedflagForge Vulkan 编辑器，场景为 Sponza-Atrium。五张客户区原图都是 1818 × 1084 像素，可以在 [output/rhi](../../../../output/rhi/) 找到。手册中的图片按下表裁剪。坐标从原图左上角起算，右、下端不包含在裁图内。

| 手册图像 | 原始文件 | 裁剪范围（左，上，右，下） |
| --- | --- | --- |
| atrium.png | editor-gi-on.png | 315, 230, 1294, 738 |
| 工作区总览 | editor-light-inspector.png | 整张原图，四个编号由 LaTeX 叠加 |
| effects-button.png | editor-light-inspector.png | 303, 88, 918, 365 |
| effects-menu.png | editor-effects.png | 663, 164, 1058, 592 |
| before.png | editor-gi-off.png | 319, 274, 906, 670 |
| after.png | editor-gi-on.png | 319, 274, 906, 670 |
| add-component.png | editor-add-component.png | 1315, 46, 1815, 322 |

第二页的截图选中了 Courtyard Lamp，强度为 28。第四页使用同一主相机、同一裁剪矩形、全部点光源、原始分辨率、开启 TAA、关闭 SSR。两次只改间接光选择。GTAO 始终勾选，在间接光关闭时该项变灰。所有裁图保持原始像素值，未单独调亮任一幅。

所用 glTF 的原始 Sponza 房间由 Marko Dabrovic 制作，Crytek 版由 Frank Meinl 重制，PBR 纹理由 Alexandre Pestana 提供。运行命令和固定资产版本在 tests/fetch_sponza.py。[原模型作者和说明](https://github.com/KhronosGroup/glTF-Sample-Assets/blob/e124e8d8d7330a5372bc2b4d00d39a4a9d7997ea/Models/Sponza/README.md) · [模型许可](https://github.com/KhronosGroup/glTF-Sample-Assets/blob/e124e8d8d7330a5372bc2b4d00d39a4a9d7997ea/Models/Sponza/LICENSE.md)

## SSAA 的界面与拱线

三张新增插图取自 Windows 独立桌面运行的 Debug x64 Vulkan 编辑器。程序 SHA-256 为 `2d6038a435fdbd064b6a63bb05bb312af39ed0ced1c57d028d46652c390a0cfc`。两次各打开一份 Sponza-Atrium 场景副本，读取相同机位，直接光选全部点光源，关闭间接光、TAA、SSR 和 GTAO，面板输出宽度为 320。第二次从场景设置读入已开启的 SSAA。右栏的高度在第二次加大，中央视口保持相同尺寸。窗口客户区为 2560 × 1440，两次视口均为 1391 × 892。截图未手动改色。

| 手册图像 | 客户区来源 | 裁剪范围（左，上，右，下） |
| --- | --- | --- |
| ssaa-sponza-off.png | 关闭 SSAA，光照图 320 × 205 | 667, 282, 1077, 649 |
| ssaa-sponza-on.png | 开启 SSAA，光照图 640 × 410 | 667, 282, 1077, 649 |
| ssaa-settings.png | 开启 SSAA 时的“质量与参数” | 1845, 916, 2547, 1195 |
