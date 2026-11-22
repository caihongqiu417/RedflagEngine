# Vulkan 计算数组的内存位置

在 RTX 5070 Ti Laptop、驱动 616.56 上，旧分配规则会把存储三角形、材质和每像素历史的缓冲区放在系统内存。设备本地内存也能在这台机器上直接映射。两种配置的内存类型和容量见[驱动输出](memory-types.txt)，编辑器每次光照累积的原始毫秒数见[同机对照](native.json)。

## 编辑器测量

保留两个使用相同 Debug 配置的 Forge 可执行文件。旧版本对 VulkanComputeDevice::CreateBuffer 中的存储数组要求 HOST_VISIBLE 和 HOST_COHERENT，按设备列出的顺序取第一个匹配项。为了从当前源码重现这一选择，可在单独的检出目录中仅把该分配调用的最后两个参数改为 HostMemory、0。本地显存版本使用当前实现。两个版本的 DLL、Sponza 模型、相机、灯光和 .render.yaml 须一致；仓库中的场景配置已经选好 Lit 和 Native。

在仓库根目录执行以下命令。测试按 A、B、B、A 次序启动独立的 Forge。每次保留 21 个光照累积样本，跳过首次画面、加载及四个预热样本。计时包含 CPU 调度及等待 GPU 完成的实际时间。采用保存的 Sponza 主机位，静止画面最终被缓存后的时间也排除。测试工作目录位于 tmp，写入的窗口布局独立于日常编辑器。

    python tests/profile_compute_memory.py D:\builds\host\RedflagForge.exe D:\builds\local\RedflagForge.exe

[第 1 轮](1-host.json.gz) · [第 2 轮](2-local.json.gz) · [第 3 轮](3-local.json.gz) · [第 4 轮](4-host.json.gz)均为实际 Chrome 时间线，解压后可用性能查看器打开。

## 移动相机的整帧时间

正式链接的 Forge 另有一次 948 × 613 的滚轮测试，完整整帧时间、P95、计入的帧数和程序 SHA-256 见[导航对照](final-navigation.json)。[强制完整光照](final-lit-motion.json.gz)与[默认导航](final-navigation.json.gz)各保存一份原始 Chrome 时间线。使用相同的 Sponza 场景和 50 次、每次相隔 150 毫秒的滚轮输入。依赖 PyYAML 与 pywin32；复现命令为 python tests/profile_native_navigation.py 路径\RedflagForge.exe。

## 小尺寸回放的 GPU 时间

对照使用同一份 288 × 192 的 Stanford Bunny 场景，每帧包含相同的 12 次计算。可打开[旧分配规则的帧](../stanford/StanfordBunny_capture.rdc)和[本地显存的帧](StanfordBunnyLocal.rdc)。[回放记录](replay.json)保存两份文件的 SHA-256、工作组数、事件编号、一次预热后的 7 次 GPU 计时和 8 张 RGBA32F 输出的 SHA-256。相同效果的整张图均逐字节一致。测试用 RenderDoc 1.47 回放。

从仓库根目录执行 qrenderdoc --python tests/replay_compute_memory.py 可以重新测量现有捕获。需要录新帧时，用各构建的 tests/run_stanford_effects.cmd --capture 传入同一 RenderDoc DLL，分别保存对应的兔子帧，再运行回放脚本。完整场景输入和图像见[Stanford 扫描模型](../../scenes/Stanford-Scans.md)。

## 另一种上传方式

为实测 CPU 不能映射的显存，独立编译同一个 VulkanComputeDevice.cpp，仅把设备本地分配的最后一个参数（首选属性）设为 0。本机因而选中显存类型 1。上传和读回必须经由暂存缓冲区。将这个对象换入 tests/run_compute_tests.cmd 所用的可执行文件，三帧、35 个数组元素以及 7 × 5 图像的所有通道均匹配，最后一个像素的红色为 112。日常构建由驱动属性自动选择分配方式。
