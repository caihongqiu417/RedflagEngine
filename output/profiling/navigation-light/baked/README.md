# 烘焙浴室的第 24 帧

两次打开 RTXGI Bathroom，使用随场景保存的 DDGI 探针。已烘焙的房间静止时，沿用第一张面板图可以省去重复的光照计算。两次截图选在第 24 帧，落在旧版的 32 帧重算区间内。

| 第 24 帧的 Vulkan 命令 | 旧版 | 新版 |
| --- | ---: | ---: |
| 计算（dispatch） | 4 | 0 |
| 场景网格绘制 | 85 | 85 |
| 界面绘制 | 22 | 22 |

在 ImGui 实际绘制场景面板时读取片元着色器采样的 RGBA32F 图像。旧版在事件 558 读图，新版在事件 510 读图。两版的 160 × 103 个像素（263,680 字节）具有相同的 SHA-256。左上角为图像原点，RGB 为面板显示值，PNG 将它们限制在 0 到 1 并按 255 换算为字节。对照使用转换前的四通道浮点数据。[旧版实拍](before.png) · [新版实拍](after.png) · [完整事件号、面板尺寸、原始帧相对路径及 SHA-256](frame24.json)

旧版事件 11 算直接光，23 读取已保存的探针给物体表面补光，35 将两项相加，47 写入面板图。两次运行的编辑器仍然逐帧绘制场景网格，生成点击物体时读取的编号。两次捕获皆未报告 Vulkan 调试消息。GPU 为 NVIDIA GeForce RTX 5070 Ti Laptop GPU，Debug x64，RenderDoc 1.47，场景面板为 1391 × 892，内部光照宽度固定为 160。这是命令与画面对照，未计 CPU 或 GPU 时间。

## 复查

先取得项目的 RTXGI Bathroom 模型及其场景旁的 `.rfprobes`。以下 PowerShell 命令从仓库根目录运行。前两条正式编辑器各自使用独立的工作目录和 Windows 桌面，只把测试场景和探针复制到临时目录的默认场景入口。两版都用 Lit、全部点光源和烘焙 DDGI，关闭 TAA、GTAO、SSR 和快速导航。

```powershell
$tools = 'tmp/tools/renderdoc/RenderDoc_1.47_64'
$before = 'tmp/material-reference/RedflagForge-material.exe'
$after = 'bin/Debug-windows-x86_64/RedflagForge/RedflagForge.exe'
python tests/capture_material_studio.py --baked-bathroom --renderdoc $tools --exe $before
python tests/capture_material_studio.py --baked-bathroom --renderdoc $tools --exe $after
python tests/replay_baked_preview.py $before $after
```

前两次运行分别自动截取并回放第 24 帧，从 ImGui 的实际取样位置导出浮点贴图。第三条命令逐像素比较两份输出，并在命令数或画面不符合预期时返回错误。可执行文件、原始帧、面板图和已烘焙探针的摘要均在 JSON 中。历史程序保存在本机上；更换可执行文件后会按新摘要另存原始帧。

两次截帧各启动 56 帧，面板完成最后一次尺寸调整后取 55 帧，引擎时间线分别记录旧版重画 32 帧、新版重画 1 帧。另可运行 `python tests/check_preview_frames.py --baked-bathroom` 看开启 TAA 和单独显示 DDGI 的 48 帧对照。[另三组运行记录](../baked-frames.json)
