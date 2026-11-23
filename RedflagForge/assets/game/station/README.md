# 潮汐维修站

在 Forge 打开 `assets/scenes/Watt-Station.redflag`，按 F6 进入第三人称游戏。出生点面向东侧维修棚，发电机在棚门附近。近处的旧推车、工具架和油桶沿路摆放，中间的空地留给玩家和队友通行。行车跨在工棚后方，最远处是采石坡。

铁皮棚由 tevy 制作。模型保留作者的门轴、拉绳和灯泡，导入时将门打开。发电机、推车、行车、吊灯和砂石取自 Poly Haven。每件道具独立保存，关卡作者可以在 Forge 中移动或替换它。[铁皮棚导入记录](salvage-kit/README.md) · [Poly Haven 模型清单](salvage/sources.json) · [砂石材质来源](salvage/ground/sources.json)

钢梁、管道和货柜取自 Godot 的第三人称样例。美术由 Juan Linietsky 与 Fernando Miguel Calabró 创作，部分材质源自 GameTextures.com，采用 CC BY 3.0。导入器从固定版本的模型中选出单个部件，保留顶点、纹理坐标和材质，并将原点移到方便摆放的位置。玩家与瓦力目前共用同一具骨架，以蓝灰色和赭色区分。取物和递物动作来自 Quaternius 的 Universal Animation Library。[关卡部件与原始版本](yard/sources.json) · [美术许可](crew/LICENSE-Godot.md) · [角色和动作](crew/README.md)

玩家所见的地面有两种来源。场内铺设的是砂石照片材质，场外的起伏则来自采石场的实测高程。选择「旧采石坡」可在地形组件中调整地表高度和贴图的实际尺寸。游戏界面的坐标和图片也单独保存在资源目录。[地形数据](../../terrain/quarry/sources.json) · [游戏界面](../../ui/station/README.md)
