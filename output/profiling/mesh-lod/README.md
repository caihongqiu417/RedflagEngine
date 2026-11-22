# Stanford 兔子的网格细节

场景使用 Stanford 扫描库的三个分辨率，`res2`、`res3` 和 `res4` 分别对应 LOD 0、1 和 2。源码所用的模型文件由下载脚本逐份校验。正式编辑器打开 `Reference-StanfordBunny.redflag`，选中兔子后可在「属性 → 模型绘制 → 网格细节 LOD」修改阈值。[Stanford 扫描库](https://graphics.stanford.edu/data/3Dscanrep/) · [场景](../../../RedflagForge/assets/scenes/Reference-StanfordBunny.redflag)

界面图取自 Debug x64 Vulkan 编辑器里选中兔子时的实际窗口。在 2560 × 1440 的截图中，按左上角 (1892, 518) 裁出 623 × 347 像素，保留了当前网格与两档切换高度。[网格细节面板](inspector.png)

以下 Windows 命令从仓库根目录运行。将第一项替换为 `OpenGL` 或 `OpenGLScene` 可分别记录基础材质与完整场景；第二项用 `full` 强制全精度，用 `auto` 让正式场景选择 LOD。为六组各截一帧后，再分别运行不带 DLL 的基础材质测试生成独立 CPU 样本。

```bat
python tests\fetch_stanford.py
tests\run_mesh_lod.cmd Vulkan auto "C:\Tools\RenderDoc\renderdoc.dll"
tests\run_mesh_lod.cmd Vulkan auto
qrenderdoc --python tests/replay_mesh_lod.py
python tests/check_mesh_lod_images.py
```

离屏图像固定 384 × 384，正交相机看向兔子，另有一块地板。CSV 的前九行逐步拉远、再拉近，其中重复出现的高度用来检验切换滞回。后 32 行是在 10% 高度下采集的 CPU 记录时间，预热的四帧已排除。实际 RDC 留在本地 `output/captures/local`。OpenGL 基础材质事件 22、Vulkan 事件 20 是兔子的绘制，另一次绘制是地板。完整 OpenGL 场景的阴影、反射阴影图、深度预写和主绘制可用原始事件编号核验。[回放和 GPU 时间](results.json) · [同机位轮廓差](silhouette.json)
