# 教材、实现与实测

从下列章节可分别打开可编辑笔记、项目源码和原作者材料。

## 从两盏灯到一个采样估计

[本章笔记](sampling.tex) · [GI.comp](../../../Redflag/src/Redflag/Renderer/GI/Shaders/GI.comp)

[PBRT 第四版 §2.1](https://pbr-book.org/4ed/Monte_Carlo_Integration/Monte_Carlo_Basics) · [§2.3 逆变换采样](https://pbr-book.org/4ed/Monte_Carlo_Integration/Sampling_Using_the_Inversion_Method) · [PBRT 源码](https://github.com/mmp/pbrt-v4) · [GAMES101 课程](https://sites.cs.ucsb.edu/~lingqi/teaching/games101.html)

[点光源下的金属高光](point-materials.tex)

## 探针怎样估计间接光

[本章笔记](ddgi.tex) · [GlobalIllumination.cpp](../../../Redflag/src/Redflag/Renderer/GI/GlobalIllumination.cpp)

[RTXGI-DDGI 源码](https://github.com/NVIDIAGameWorks/RTXGI-DDGI) · [NVIDIA 的探针更新与遮挡说明](https://github.com/NVIDIAGameWorks/RTXGI-DDGI/blob/main/docs/Algorithms.md) · [JCGT 2019 原论文（未列 DOI）](https://jcgt.org/published/0008/02/01/) · [GDC 的探针课](https://www.gdcvault.com/play/1026182/Ray-Traced-Irradiance-Fields-%28Presented) · [GAMES202 的 RSM](https://sites.cs.ucsb.edu/~lingqi/teaching/resources/GAMES202_Lecture_07.pdf) · [PBRT 漫反射](https://pbr-book.org/4ed/Reflection_Models/Diffuse_Reflection)

[墙面之间的多次漫反射](probe-feedback.tex) · [窗外的环境光](sky-probes.tex) · [保存和载入烘焙探针](probe-baking.tex)

## ReSTIR DI 的选灯与复用

[本章笔记](restir-di.tex) · [GI.comp](../../../Redflag/src/Redflag/Renderer/GI/Shaders/GI.comp)

[RTXDI 集成代码](https://github.com/NVIDIA-RTX/RTXDI) · [NVIDIA 的集成指南](https://github.com/NVIDIA-RTX/RTXDI/blob/main/Doc/Integration.md) · [论文 DOI 10.1145/3386569.3392481](https://doi.org/10.1145/3386569.3392481) · [SIGGRAPH 的 ReSTIR 入门课](https://intro-to-restir.cwyman.org/) · [PBRT 提高采样效率](https://pbr-book.org/4ed/Monte_Carlo_Integration/Improving_Efficiency)

## ReSTIR GI 的二次交点

[本章笔记](restir-gi.tex) · [GIScene.cpp](../../../Redflag/src/Redflag/Renderer/GI/GIScene.cpp) · [GI.comp](../../../Redflag/src/Redflag/Renderer/GI/Shaders/GI.comp)

[RTXDI 的 GI 集成](https://github.com/NVIDIA-RTX/RTXDI/blob/main/Doc/RestirGI.md) · [论文 DOI 10.1111/cgf.14378](https://doi.org/10.1111/cgf.14378) · [ReSTIR 课程讲义](https://intro-to-restir.cwyman.org/) · [PBRT 概率换元](https://pbr-book.org/4ed/Monte_Carlo_Integration/Transforming_between_Distributions) · [PBRT 第四版 §13.4 路径追踪](https://pbr-book.org/4ed/Light_Transport_I_Surface_Reflection/A_Better_Path_Tracer)

## GTAO 的遮挡轮廓

[本章笔记](gtao.tex) · [GTAO.comp](../../../Redflag/src/Redflag/Renderer/ScreenSpace/Shaders/GTAO.comp)

[Intel XeGTAO](https://github.com/GameTechDev/XeGTAO) · [Activision 技术报告](https://www.activision.com/cdn/research/PracticalRealtimeStrategiesTRfinal.pdf) · [SIGGRAPH 2016 讲义](https://blog.selfshadow.com/publications/s2016-shading-course/activision/s2016_pbs_activision_occlusion.pdf) · [LearnOpenGL 的 SSAO](https://learnopengl.com/Advanced-Lighting/SSAO)

## TAA 的采样位置与重投影

[本章笔记](taa.tex) · [TAA.comp](../../../Redflag/src/Redflag/Renderer/ScreenSpace/Shaders/TAA.comp)

[Unity URP 源码](https://github.com/Unity-Technologies/Graphics/blob/master/Packages/com.unity.render-pipelines.universal/Shaders/PostProcessing/TemporalAA.shader) · [综述 DOI 10.1111/cgf.14018](https://doi.org/10.1111/cgf.14018) · [Karis，SIGGRAPH 2014](https://advances.realtimerendering.com/s2014/) · [GAMES101 变换](https://sites.cs.ucsb.edu/~lingqi/teaching/resources/GAMES101_Lecture_04.pdf) · [RTR4 附录 A](https://www.realtimerendering.com/Real-Time_Rendering_4th-Appendices.pdf)

## 分簇筛选点光源

[本章笔记](cluster.tex) · [ClusterCull.comp](../../../Redflag/src/Redflag/Renderer/ScreenSpace/Shaders/ClusterCull.comp) · [ClusterShade.comp](../../../Redflag/src/Redflag/Renderer/ScreenSpace/Shaders/ClusterShade.comp)

[Filament 的 Froxelizer](https://github.com/google/filament/blob/main/filament/src/Froxelizer.cpp) · [HPG 论文 DOI 10.2312/EGGH/HPG12/087-096](https://doi.org/10.2312/EGGH/HPG12/087-096) · [SIGGRAPH 2012 报告](https://doi.org/10.1145/2343045.2343095) · [LearnOpenGL 延迟着色](https://learnopengl.com/Advanced-Lighting/Deferred-Shading)

## 粗糙表面的随机屏幕反射

[本章笔记](ssr.tex) · [StochasticSSR.comp](../../../Redflag/src/Redflag/Renderer/ScreenSpace/Shaders/StochasticSSR.comp)

[AMD SSSR 源码](https://github.com/GPUOpen-Effects/FidelityFX-SSSR) · [JCGT 屏幕空间射线论文](https://jcgt.org/published/0003/04/04/) · [Stachowiak 的 SIGGRAPH 课程](https://h3.gd/stochastic-ssr/) · [PBRT 微表面理论](https://pbr-book.org/4ed/Reflection_Models/Roughness_Using_Microfacet_Theory) · [GAMES202 课件](https://sites.cs.ucsb.edu/~lingqi/teaching/games202.html)

[屏幕外的倒影与遮挡](mirror.tex) · [PBRT 镜面反射](https://pbr-book.org/4ed/Reflection_Models/Specular_Reflection_and_Transmission) · [AMD 混合反射](https://gpuopen.com/manuals/fidelityfx_sdk/samples/hybrid-reflections/)

[粗糙倒影的降噪](rough-reflections.tex) · [反射滤波着色器](../../../Redflag/src/Redflag/Renderer/ScreenSpace/Shaders/ReflectionFilter.comp) · [HPG 2010 作者论文](https://jo.dreggn.org/home/2010_atrous.pdf) · [AMD 反射降噪说明](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/denoiser/)

## 方向光与投影

[Stanford Bunny 的同机位截帧](directional.tex) · [PBRT 第四版 §12.3](https://pbr-book.org/4ed/Light_Sources/Distant_Lights)

## 光谱、色散、球谐函数与体素

[专题与配图](light-representations.tex) · [棱镜插图的原件和许可](figures/README.md)

[PBRT 第四版，光谱分布](https://pbr-book.org/4ed/Radiometry,_Spectra,_and_Color/Representing_Spectral_Distributions) · [PBRT 第四版，玻璃色散](https://pbr-book.org/4ed/Reflection_Models/Specular_Reflection_and_Transmission) · [漫反射环境光的球谐近似](https://graphics.stanford.edu/papers/envmap/) · [三角网格的体素化](https://michael-schwarz.com/research/publ/2010/vox/)

## 镜头外的灯牌

[光照积分、探针实拍与读数](emissive-panel.tex) · [PBRT 第四版 §12.4](https://pbr-book.org/4ed/Light_Sources/Area_Lights)

## 光线与场景中的三角形

[包围盒与空走廊示意图](triangle-groups.tex) · [PBRT 第四版 §7.3](https://pbr-book.org/4ed/Primitives_and_Intersection_Acceleration/Bounding_Volume_Hierarchies)

