# 两层玻璃的 OpenGL 回读

在 384 × 216 的正交相机前摆两张倾斜的玻璃。格纹墙位于两张玻璃之后，绿色方柱在它们前面。三张 PNG 都从引擎的 HDR 颜色图读回，统一用 `x/(1+x)` 调曝光，再用 `1/2.2` 转为屏幕亮度。比较数值时用转换前的线性 RGB。

| 文件 | 可以核对什么 |
| --- | --- |
| opaque.png | 取走两张玻璃，保留背景和方柱。 |
| straight.png | 显示粉、蓝两张玻璃。折射率为 1，竖线仍在原处。 |
| refracted.png | 折射率 1.52、法向厚度 0.45，两层后面的网格发生位移。 |
| measurements.csv | 逐像素颜色、拾取编号、深度和边缘检查。 |
| GL-Glass_capture.rdc、renderdoc.json | RenderDoc 1.47 帧与回放事件。事件 10、17 画格纹墙和柱子，54、62 把两层玻璃累加进三张浮点图，75 把颜色写回主画面。 |

换两张玻璃的提交顺序，82,944 个像素的颜色不变。折射率为 1 或厚度为 0 时，与没有背景位移的图逐像素相同。2,074 个方柱像素保持原色，整幅画面的拾取编号与深度和移走玻璃时相同。另外将一片网格翻面，单面玻璃消失，开启双面后重新可见。

倾斜的玻璃会从相邻四个像素读取背景。若只检查其中最近的一个，另外三个可能混进前景柱的颜色。保持几何不动，把绿柱换成紫柱，在厚度 0.16、0.32、0.45、0.60 下逐个检查柱外像素。旧写法中柱外某一颜色通道的最大变化是 0.311768。着色器逐点比较深度，遇到比玻璃更近的像素就取原处背景。相同测试的最大变化为 0。

复现时在仓库根目录运行 `tests/run_opengl_transparency_tests.cmd`。传入 RenderDoc 的 `renderdoc.dll` 绝对路径会另行截帧。本机使用 Debug x64、RTX 5070 Ti Laptop GPU 和 OpenGL 4.6。该测试还用 RGBA8 的四重采样目标与默认窗口检查完整性和状态恢复。此处记录的是画面正确性。

- [两片玻璃的材质、相机与断言](../../../tests/OpenGLTransparencyTests.cpp)
- [场景背景与累加目标](../../../Redflag/src/Platform/OpenGL/OpenGLTransparencyPass.cpp)
- [表面光照和权重](../../../RedflagForge/assets/shaders/Renderer3D_Mesh.glsl)
- [四个采样点的颜色和深度](../../../RedflagForge/assets/shaders/Transparency_Composite.glsl)
