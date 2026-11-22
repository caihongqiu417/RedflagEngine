# 移动时的完整光照与直接光

在仓库根目录运行 `python tests/profile_native_navigation.py bin/Debug-windows-x86_64/RedflagForge/RedflagForge.exe`。脚本在独立的工作目录复制 Sponza 场景和配置，校准实际面板到 948 × 613。先等稳定画面，再每隔 150 毫秒发送一次滚轮输入，五次拉近、五次拉远，共 50 次。退出前额外滚动一格，在相同位置从窗口读取运动图和停手图，这组截图在计时区间以外。窗口截图为 1763 × 1079，场景面板左上角在 (305, 214)。

同一 Debug x64 可执行文件，NVIDIA RTX 5070 Ti Laptop GPU，Vulkan 验证开启。完整光照包含全部三盏点光源、每方向两条射线的 DDGI、GTAO 和权重 0.85 的 TAA。实时效果均沿用相同场景设置。两轮各识别到 50 个重新计算的运动帧，按整帧 RunLoop 的耗时取中位数和 P95。实测值和可执行文件签名见 [final-navigation.json](final-navigation.json)，两份 gzip 存放运行时 Chrome trace。

[第 39 帧回放记录](renderdoc.json)和[导出窗口](renderdoc.png)来自单独的一次 RenderDoc 1.47 运行。快速导航开启，继续滚动时截帧。其窗口为 2560 × 1440、场景面板为 1393 × 892。事件 12 写入可见表面，24 计算直接光，36 生成显示纹理。完整帧在本机 `output/captures/local/Forge-Sponza-Navigation-39.rdc`，约 588 MB，SHA-256 见 JSON。

此前显存优化所用的[旧导航基线](../compute-memory/final-navigation.json)采用材质底色作为运动视图，保存在原目录。该基线的 1819 个导航帧包含滚轮输入之间的帧；新统计将真正计算直接光的 50 帧与只显示缓存的帧分开。
## 镜头不动时实际画了几帧

在仓库根目录运行 `python tests/check_preview_frames.py`。测试分别以五套设置打开 Sponza 的组合光照，固定内部宽度 160 像素，让每个 Vulkan 编辑器运行 48 帧。启动时面板会随布局改变一次大小，因此从最后一次相机投影改变开始数。直接光和分簇光照各重画一次，随后 46 帧显示同一张图。单独启用 ReSTIR DI、TAA 或实时 DDGI 时，各重画 32 次，随后 15 帧显示同一张图。这个实验查的是编辑器执行了几次光照计算。要判断取样是否足够，还要用同一机位的参考图比较误差。[实测事件数与程序摘要](preview-frames.json)

浴室旁边有一份已经算好的 DDGI 探针。关闭 TAA，保持镜头和灯光不动，每次从这些探针读取的光都相同。旧版只看 DDGI 开关，画完第一帧又重算了 31 帧。现在 `LightingPreview::UsesFrameHistory` 会先看场景是否读到了烘焙文件。正式编辑器连续运行 48 帧，其中第一帧用于调整面板。余下 47 帧，无论选择单独的 DDGI 视图，还是在组合光照里开启 DDGI，都只计算一次光照，随后 46 帧继续显示这张图。打开 TAA 后，同一房间画了 32 次，因为这时每帧还要在像素内换个位置取色。运行 `python tests/check_preview_frames.py --baked-bathroom` 可复查这三种设置。[浴室逐项计数](baked-frames.json) · [第 24 帧的 RenderDoc 对照](baked/README.md)
