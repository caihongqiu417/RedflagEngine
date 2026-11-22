# 铁皮棚

在场景中以比例 `(1,1,1)` 导入 `ScrapShanty.glb`。门面朝 +Z，+Y 向上，原点位于棚体中部的地面。四周最远的点包含雨棚拉绳。沿 X 从 −2.838 到 2.682 米，沿 Z 从 −2.324 到 4.462 米，高 3.346 米。屋外的车门属于侧墙，雨棚下方才是入口。作者保留了单独的门轴，本版沿该轴向外打开 58°。放置碰撞体时按棚墙和地板分别放置，可以留下门洞。

模型由 tevy 发布，名称为 Scrap Shanty v1，按 CC0 1.0 提供。这是一件按实物比例制作的模型。来源没有称它是摄影测量扫描。本版保留最高精度的 40,822 个三角形，保留原材质的颜色、法线、粗糙度、灯泡发光和玻璃参数。铁皮主体沿用作者给的金属度 0。游戏材质是否显示玻璃透射取决于使用的后端。[作者页面](https://tevy.dev/models/scrap-shanty) · [模型版本和尺寸](https://tevy.dev/models/scrap-shanty.json) · [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/)

## 重新导入

从仓库根目录执行以下命令。工具只在本机的临时目录安装。导入器按作者公布的 SHA-256 核对下载，解开压缩的顶点，把最高分辨率纹理转为 PNG，再按门轴旋转门板并对齐地面。现成的 GLB 可以直接再分发，无须安装这些工具。

```powershell
npm install --prefix tmp/salvage-source --ignore-scripts @gltf-transform/core@4.2.1 @gltf-transform/extensions@4.2.1 @gltf-transform/functions@4.2.1 meshoptimizer@0.24.0 basisu@1.16.3
node scripts/import_station_buildings.mjs tmp/salvage-source
```

转换器也处理东侧仓库、值班塔、检修台、路肩碎石、草簇和刺灌。这些模型同属 tevy 的 CC0 资源。近处的铁皮棚和检修台用了细节最多的版本，仓库、值班塔和远处刺灌选用作者提供的较简版本。纹理保留透明通道。各件的上游版本、校验值、转换后的大小和实际尺寸见清单。[固定版本铁皮棚](https://tevy.dev/models/scrap-shanty/v1/lod0.glb) · [项目导入记录](sources.json)
