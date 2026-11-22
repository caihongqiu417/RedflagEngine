# DDGI 的单次与多次漫反射

在正式 Vulkan 编辑器的「实时效果」选择 DDGI，或在组合光照 Lit 内把间接光选成 DDGI。取消「多次漫反射」可以对照同一间房的一次反光。切换后，编辑器按选定模式查找场景旁的烘焙文件；找不到匹配文件就实时更新。点击「烘焙探针」才会保存新模式的结果。保存场景也会保存该开关。

## 从地面和侧墙到后墙

三个立方体压成红色地面 A、浅灰侧墙 B 和灰色后墙 C。测试按同一相机、灯和种子 137 渲染 65 × 65 像素，中心 (32,32) 应落在世界坐标 (0,1,−2) 的 C 上。A、B 分别能否反光，共四种组合；变黑只改变漫反射率，其网格仍遮挡光线。[逐帧线性 RGB](samples.csv) 中 mode 0 对应一次，1 开启多次。reflectors 的最低位设置 A、第二位设置 B，update 1–4 是连续实时帧，5 为从头计算的静态烘焙。

取 C 上的红通道，两个反光面的联合贡献为 `I(AB) − I(A) − I(B) + I(黑)`。关闭反馈时，所有结果与零相差不到 2×10⁻⁸。开启后，第一帧仍为零，第二帧约为 0.004959，第四帧 0.012139；静态四轮 0.029409。两种模式的采样方向、射线数、几何和位姿保持一致。若把灯变亮 32 倍，第四帧中心红通道从 0.228865 变为 7.32368，途中移动相机再返回仍得到该值。切换一次/多次会拒绝不相符的 .rfprobes，再切回可以逐位读回原结果。

在仓库根目录运行 `tests\run_gi_benchmark.cmd --probe-feedback`。原始输出在 `tmp/gi-benchmark/probe-feedback`。使用 RenderDoc 捕获时，开启本机的 Vulkan 捕获层，把已安装的 `renderdoc.dll` 作为最后一个实参。RenderDoc 1.47 的两个真实帧分别保存单次和多次模式的第四轮更新，均对 3,024 个方向各发 64 条射线。运行 `qrenderdoc --python tests/replay_probe_feedback.py` 可以核对着色器工作参数并重新获得 11 组 GPU 时间戳；单次预热不计。[捕获位置、SHA-256 与计时](replay.json)

## Sponza 的固定机位

[一次反射](sponza-single.png)和[多次反射](sponza-multi.png)均为正式编辑器 DDGI 单项的第 24 帧。相机和灯光取自 `Sponza-Atrium.redflag`，每方向八条射线，内部输出宽 800 像素。两次取图都从显示着色器写完后导出，没有加曝光。从线性输出读数则回到第 23 号事件，其位置在显示变换之前。[画幅、线性 RGB、文件与帧哈希](sponza.json)

几何出自 Crytek Sponza 的公开 glTF 样例。捕获与颜色差由 Redflag 运行得到，场景原作者和各资源许可见 Khronos 的发布说明。[模型来源与署名](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Sponza)

## 算法与源码

- Majercik 等在 JCGT 8(2)，2019 年，第 1–30 页讨论了探针之间的漫反射传播、距离矩和漏光。该刊条目提供卷期和页码。[原论文](https://jcgt.org/published/0008/02/01/)
- NVIDIA 的命中着色器在第 146–180 行合成直接光与来自探针的间接光，并对用于反馈的反射率封顶 0.9。Redflag 的六方向探针保存余弦采样后的 E/π，测试场景只把材质反射率限制在 [0,1]，不把 HDR 辐亮度截在 1。[官方示例，固定提交](https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/f33e496ca31b3f0eec1c4e2cbaa8bb620e337fa6/samples/test-harness/shaders/ddgi/ProbeTraceRGS.hlsl#L146-L180)
- GDC 2019 的演讲介绍动态探针与实时渲染的组合。可结合本文的两张中庭实机图阅读。[Morgan McGuire 的演讲](https://gdcvault.com/play/1026182/)
- 实际读取旧探针、生成新探针的代码在 [Probes.glsl](../../../Redflag/src/Redflag/Renderer/GI/Shaders/Probes.glsl)，切换两份数组、清掉旧照明、检查烘焙模式的入口在 [GlobalIllumination.cpp](../../../Redflag/src/Redflag/Renderer/GI/GlobalIllumination.cpp)。[确定性三墙测试](../../../tests/ProbeFeedbackCheck.h)
