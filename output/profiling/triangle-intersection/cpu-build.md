# 从模型生成射线要查的包围盒

Sun Temple 有 606,376 个三角形。一条射线如果错过包围一整片屋顶的盒子，就可以一次跳过里面所有三角形。原来的建树方式沿三角形重心分布最宽的一轴，将它们按数量对半分。现在先沿同一轴划出 12 段，把每个切分位置左右两侧的盒子表面积分别乘以所含三角形数，选择总和最小的一处。这样一条射线更有机会在大组几何之外提前通过。重心重合或还剩 7 到 12 个三角形时直接对半分，最底层一个盒子最多包含 6 个三角形。代码中的面积省去了公共乘数 2，比较结果相同。[PBRT 第四版 §7.3.2，图 7.7](https://pbr-book.org/4ed/Primitives_and_Intersection_Acceleration/Bounding_Volume_Hierarchies) · [建树源码](../../../Redflag/src/Redflag/Renderer/GI/GIScene.cpp)

| 场景 | 三角形数 | 对半分 CPU 中位数 | 12 段 CPU 中位数 | 分组前后的节点数 |
| --- | ---: | ---: | ---: | ---: |
| Sun Temple | 606,376 | 5,227.89 ms | 3,065.61 ms | 262,143 → 260,265 |
| Sponza Atrium | 262,267 | 2,281.39 ms | 1,283.74 ms | 131,071 → 111,903 |

Windows、AMD Ryzen 9 8945HX、MSVC 14.44.35207，Debug x64，关闭编译器优化。对每个场景只导入一次几何，保留三角形进入建树函数时的顺序和世界坐标。Sun Temple 的模型使用单位矩阵，Sponza 沿三个轴等比缩放 0.671888709 倍，再平移 (0.325295836, −3.50185919, 0.207966045)。每种算法各先预热一遍，再各测五遍。两种算法在同一进程成对执行，每对调换先后顺序，两边都从同一份三角形副本和空的节点数组开始。[逐次读数](cpu-build.csv) · [Sponza 场景配置](../../../RedflagForge/assets/scenes/Sponza-Atrium.redflag)

计时用 C++ 的 `std::chrono::steady_clock`，从计算三角形包围盒前开始，到最后一个节点和排序后的三角形数组可以交给 GPU 为止。12 段版本还会预先保存重心、包围盒和三角形原序号，递归切分时沿用。表里的时间包含这笔额外准备和最后一次搬动完整三角形的代价。它们是建树耗时，磁盘读取、解码图片和上传 GPU 另外发生。

要在这台机器或另一台 Windows 电脑上核验当前版本，可以按同目录的捕获说明打开场景，查看这一轮的 `RedflagProfile-Runtime.json`。在 Chrome 时间线中搜索 `GI / Build BVH` 即可找到正式编辑器初次建树的耗时。也可以从仓库根目录运行下面的 PowerShell。耗时字段 `dur` 的单位是微秒，除以 1,000 换算成毫秒。两份本次截帧的实际加载分别为 Sun Temple 3,862.012 ms、Sponza 1,688.431 ms。这是带编辑器和截帧工具的一次启动，表格给出的是独立 CPU 重复运行的中位数。[捕获步骤和 GPU 实测](README.md)

```powershell
$version = (Get-FileHash bin/Debug-windows-x86_64/RedflagForge/RedflagForge.exe).Hash.ToLower().Substring(0,12)
foreach ($scene in 'Sun-Temple','Sponza-Atrium') {
    $path = "tmp/material-reference/$scene-rays-$version-320/RedflagProfile-Runtime.json"
    $events = (Get-Content -Raw $path | ConvertFrom-Json).traceEvents
    $events | Where-Object name -eq 'GI / Build BVH' | ForEach-Object {
        [pscustomobject]@{ Scene = $scene; BuildMilliseconds = $_.dur / 1000 }
    }
}
```
