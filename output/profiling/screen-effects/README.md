# Sponza 的屏幕效果回放

在 RenderDoc 1.47 打开 `SponzaScreen_capture.rdc`。事件 8 计算环境遮蔽，20 搜索屏幕反射，32 混合两帧颜色。事件 47 为 1,056 个空间小块准备候选灯表，58 逐像素读取候选灯并求照明。四张颜色图和四张诊断图分别导自各事件的真实缓冲区。

输入为 161×91 的 Sponza，前三项使用 16 盏固定灯算出的直接光，分簇测试使用 49 盏点光。TAA 捕获前先在一个略有偏移的采样位置画一帧。`comparison.json` 将遮蔽、反射与分簇结果同独立 GPU 读回逐值比较。TAA 的八帧参考对照另见 `output/screen-effects/metrics.json`。`renderdoc.json` 保存原始帧摘要和各事件的九轮回放 GPU 计时。

## 复现命令

下列 PowerShell 命令从仓库根目录运行。设备、构建配置、工具版本、模型来源和模型许可见[同次 GI 试验](../gi/README.md)。

```powershell
python tests/fetch_sponza.py
tests/run_screen_effects.cmd
$rd = (Resolve-Path tmp/tools/renderdoc/RenderDoc_1.47_64).Path
$env:VK_ADD_LAYER_PATH = $rd
$env:VK_INSTANCE_LAYERS = 'VK_LAYER_RENDERDOC_Capture'
tests/run_screen_effects.cmd --capture "$rd/renderdoc.dll"
Remove-Item Env:VK_INSTANCE_LAYERS, Env:VK_ADD_LAYER_PATH
Start-Process "$rd/qrenderdoc.exe" -ArgumentList '--python tests/replay_screen_effects.py' -WindowStyle Hidden -Wait
python tests/analyze_screen_capture.py
```
