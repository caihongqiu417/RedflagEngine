# 手持道具对帧时间的影响

维修站的医疗包分成三块网格，分别有 1182、840、240 个索引。近身操作时，玩家从腰侧取包，药包随手移动。旧程序每次移动它都更新了静态三角形列表。新版 G-buffer 和动态方向光阴影继续绘制药包，静态场景的光线查询只保留留在原地的物件。[自疗生效时的同机位画面](hand-at-contact.jpg)

从同一份 Yard-05 文件启动游戏。只替换 `RedflagForge.exe`，让旧程序读取相同的模型、脚本、视口、相机、灯和显示设置。解压副本里把玩家生命设为 40，操作先走近、停步，再按 H。记录设备为 Ryzen 9 8945HX 和 RTX 5070 Ti Laptop，NVIDIA 驱动 616.56，Windows x64 Release。窗口为 1920 × 1080，游戏内部绘制 1280 × 720。两版分别测两次，顺序为旧、新、新、旧。

| 顺序 | 动作区间的整帧样本数 | 整帧中位 / 最大，ms | 建树次数 | 主线程绘制可见表面，中位 ms |
|---|---:|---:|---:|---:|
| 旧 1 | 5 | 1810.99 / 1877.89 | 4 | 1.77 |
| 新 2 | 37 | 65.49 / 74.91 | 0 | 1.48 |
| 新 3 | 34 | 69.85 / 84.71 | 0 | 1.61 |
| 旧 4 | 5 | 2139.57 / 2253.82 | 4 | 2.10 |

从首次出现手持状态到消失后的首帧，以引擎 CPU 跟踪里的 `RunLoop` 计整帧耗时。每个样本包括主线程更新、绘制、等待和显示。`GI / Build BVH` 只包含构建三角形索引树的那一段。旧版两轮实际建树中位分别为 176 和 224 ms，主循环还要处理加载、转换、写入资源和同步。CPU 事件 `Rasterize lighting surfaces` 包含主表面绘制与后端调用，当前跟踪无法分开纯驱动提交时间。四轮冷启动的第一帧均在统计区间之前。

RenderDoc 1.47 另抓两边第 8 帧。在仅用于查绘制事件的两次运行中，地面那份医疗包仍保留实体验证，但关闭它的网格以辨认手持的同款模型。旧程序两个相机绘制分别从事件 220、1550 开始，新程序从 187、1508 开始。新版新增的 1204、1209、1214 把三块网格写进 512 × 512 动态方向光阴影。旧捕获有 395 次绘制、20 次分派；新捕获有 398 次绘制、16 次分派。预热一次后各回放五次，绘制与计算事件的 GPU 时间之和中位为 24.44、27.35 ms。该时长不包括 CPU 重建和画面显示。两份回放均无中、高级诊断。屏幕外的静态光线反射以及离线探针不追踪手持物的运动。

[结果、帧号、程序和 RDC 的 SHA-256](results.json) 包含四轮原始样本与各次 GPU 计数。旁边每一对 `*-cpu.json.gz` 与 `*-play.jsonl.gz` 是一局原始 CPU 跟踪和角色逐帧记录。解压 CPU 文件后可直接用 Chrome 跟踪工具打开。大容量捕获在仓库本机的 `output/captures/local/station-interactions/rdc-control/hand_frame8.rdc` 和 `rdc-new/hand_frame8.rdc`。

## 复测

在仓库根目录的 Windows PowerShell 运行。准备同一个新包的两份副本，仅将对照副本的 `RedflagForge.exe` 换为旧程序。`tests/capture_station_hands.py` 使用 pywin32、Pillow 和 PyYAML。RenderDoc 的便携目录可通过 `--renderdoc` 指定。下面依次取得同一动作的两种 GPU 捕获，再运行两轮交错顺序的 CPU 跟踪。

```powershell
$raw = 'output/captures/local/station-interactions'
$old = 'tmp/Redflag-Hand-Benchmark-Before.zip'
$new = 'tmp/Redflag-Watt-Yard-05.zip'
python tests/capture_hand_contact_rdc.py $old "$raw/rdc-control"
python tests/capture_hand_contact_rdc.py $new "$raw/rdc-new"
$rounds = @('before','after','after','before')
for ($i = 0; $i -lt $rounds.Length; $i++) {
    $version = $rounds[$i]
    $build = if ($version -eq 'before') { $old } else { $new }
    python tests/capture_station_hands.py $build "$raw/trace-$i-$version" --cases player-heal
}
python tests/measure_station_hand_frames.py $raw output/validation/packaged-watt/hand-actions/performance
```

重新取样请选择一个新目录，避免混入上次采集的帧。仓库中的测量入口与本节用过的原始输入分别以 `results.json` 和对应 SHA 校验。
