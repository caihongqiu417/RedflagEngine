# 体积雾与光柱

在仓库根目录运行 `tests\run_fog_tests.cmd`，或传入本机 RenderDoc DLL 的完整路径以截取雾计算。测试建立正交相机、点光源和开孔的墙，用引擎的 Vulkan 计算后端渲染。图像以和编辑器相同的 Reinhard 映射、1/2.2 显示指数导出。亮度检查则直接从线性浮点输出读数。无灯时用白色表面对照 Beer–Lambert 公式，封孔时用同一位置复查点光源的遮挡。

[测试实测](measurements.csv)列出全部像素的最大透射误差、三种步数的 20 次调度与等待中位数，以及测试像素 (288,108) 开孔与封孔时的结果。[RenderDoc 捕获](Fog-Aperture_capture.rdc)只含单次 32 步雾计算，可查看事件 8 的 7 个存储缓冲区。第 6 号绑定是线性输出。图像尺寸和工作组数、读取该缓冲区得到的红色分量、九次回放时间戳以及帧的 SHA-256 均在[回放记录](renderdoc.json)。

两张 Sponza 图另由正式编辑器的原生窗口在同一机位取得；面板剪裁、照明设置、可执行文件和各张截图的 SHA-256 见[截图记录](evidence.json)。场景为项目的 Sponza-Atrium，模型来源为 [Khronos glTF Sample Assets / Sponza](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/e124e8d8d7330a5372bc2b4d00d39a4a9d7997ea/Models/Sponza)。
