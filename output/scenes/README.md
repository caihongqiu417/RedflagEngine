# 经典场景实测

这里保存的是 Redflag 实际导入模型后，经 SceneRenderer 绘制的图像。PNG 直接读取最终 LDR 纹理，仅翻转 OpenGL 行方向。没有离线补色、图像生成或降噪。两次运行都先保存 `.redflag`，再由 SceneSerializer 重新打开，最后绘制重新打开的场景。

| 场景 | 画面 | 子网格 | 引擎 DrawCalls | 相机剔除 | 可见几何像素 |
|---|---|---:|---:|---:|---:|
| Cornell Box | 1024 × 768 | 5 | 22 | 0 | 397212 |
| Sponza | 1280 × 720 | 103 | 372 | 21 | 912256 |

两图 HDR 附件均无 NaN/Inf，渲染与读回没有 OpenGL 错误。设备为 NVIDIA GeForce RTX 5070 Ti Laptop GPU，OpenGL 4.6.0 NVIDIA 616.56。这里没有 GPU 耗时测量。引擎计数不包括所有全屏绘制，不能直接当作完整 API draw 数量。

## 在编辑器和测试程序中加载场景

在仓库根目录构建 Debug/x64 引擎后执行：

```bat
tests\run_scene_gallery.cmd tests\scenes\cornell.yaml
tests\run_scene_gallery.cmd tests\scenes\sponza.yaml
```

配置在 `tests/scenes/`，实际参数与结果在本目录同名 YAML。模型路径相对配置文件；运行器会切换到 RedflagForge 工作目录。大模型的来源和许可见 `RedflagForge/assets/models/reference/` 内对应说明，Sponza 依赖须先取得。

Forge 可直接打开 `assets/scenes/Reference-CornellBox.redflag` 和 `assets/scenes/Reference-Sponza.redflag`。场景包含模型、相机、方向光和点光；渲染器全局开关、曝光、阴影范围及 RSM 参数仍记录在 YAML，当前场景格式没有保存这些全局设置。运行器会按 YAML 重新导出示例场景；手动编辑请另存新名字。示例实体 UUID 固定，未改配置时连续导出的场景 SHA256 相同，见 `scene-stability.json`。

Cornell 使用方向光和点光近似照明，顶板不是实际面积光源；Sponza 加了点光以便观察内廊。两者关闭 IBL 和 Bloom，开启 SSAO 与 RSM，使用修正后的 RSM 强度 1。Cornell 方向光改为 `[-0.6,-0.6,-1]`，使红墙受到直接光照；只有方向光进入当前 RSM，点光不产生这一通道的间接光。它们用于检查导入和当前光栅管线，不是 Cornell 物理参考图。图中亮缝、接触区域和点状噪声是当前实际结果，不能据此认为间接光或材质已经达到参考质量。旧参考图和同参数数值对照保存在 `rsm/`。

## Cornell Box 的 RenderDoc 帧回放

截帧使用 Rider 自带 RenderDoc 1.33.0.0 DLL，应用接口 1.6.0；DLL 在创建图形上下文前加载。原始命令：

```bat
tests\run_scene_gallery.cmd tests\scenes\cornell.yaml --capture "D:/ProgramFiles/JetBrains/JetBrains Rider 2025.3.1/plugins/rider-plugins-renderdoc/runtime/windows-x86_64/renderdoc.dll"
```

`EndFrameCapture` 和 `GetCapture` 均成功，文件为 `CornellBox_capture.rdc`。独立 RenderDoc 1.47 开启 API validation 后实际回放，脚本 `tests/replay_scene_capture.py`，在仓库根目录用 `qrenderdoc --python tests/replay_scene_capture.py` 执行。`CornellBox-replay.png` 是该回放的最终附件导出，与 `CornellBox.png` 的文件 SHA256 完全相同；它不是 RenderDoc 界面截图。

| 事件 | 实际绘制 |
|---|---|
| 37–65 | 阴影，5 个子网格、96 个索引 |
| 81–109 | RSM，5 个子网格、96 个索引 |
| 125–153 | 深度和法线，5 个子网格、96 个索引 |
| 177 / 200 / 215 | SSAO / RSM 间接光 / 时域累积 |
| 228–256 | 主绘制，5 个子网格、96 个索引 |
| 273 | 色调映射，1024 × 768 RGBA8 输出 |

RenderDoc 共记录 24 次 draw。回放实体附件有 397212 个几何像素，与原运行一致。捕获内有 67 条信息和 15 条中等级性能提示，内容包括着色器随 GL 状态重新编译、缓冲在显存和主存间迁移；回放新增 12 条信息，未出现高等级错误，fatal status 为 Success。这些提示不等于测得性能瓶颈。完整事件和消息在 `CornellBox-replay.json`，原始运行日志与文件大小、SHA256 也保存在本目录。

Sponza 没有保存大型 RDC。这里的通过仅证明模型导入、场景重开和实际渲染链路工作；不代表对场景每一处遮挡、纹理和光照进行了正确性证明。
