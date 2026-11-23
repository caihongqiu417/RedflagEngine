# 维修站的地面与金属道具

从开场的第三人称镜头看，地面占了大半个画面，工具台只有一小块。走进维修棚，距离倒过来了：近处是工具台的搁板和立柱，远处的地面只剩一道细带。两处需要保留的细节不同。我们先确定玩家在什么位置能看清一个物体，再决定把贴图预算放在哪里。

Epic 在《Fortnite》的 *Shadows of the Abbey* 示例里，把砌墙模块的大面积颜色与反复使用的裂纹、磨损分开存，细节还会随距离淡出。Quixel 处理扫描模型时则给不同精度的网格烘焙切线法线，让较少的三角形也能呈现原扫描的受光变化。维修站借鉴的是这两种取舍。某件道具本身已有合适的扫描图，就从作者的原始数据做；几件道具反复出现的污渍，可以用一份贴花。[Epic 的岛屿制作说明](https://dev.epicgames.com/documentation/fortnite/shadows-of-the-abbey-island-in-fortnite) · [Quixel 的资产处理过程](https://dev.epicgames.com/community/learning/courses/y73/unreal-engine-realityscan-asset-processing-by-quixel/zrwq/unreal-engine-realityscan-processing-steps)

| 玩家看到的东西 | 先看哪份数据 | 选材时看什么 |
| --- | --- | --- |
| 脚边的碎石与车辙 | 同一片路面的底色、法线、粗糙度、扫描高度 | 正面看石子的明暗，斜着看石子遮住沟底的程度。四个顶点铺成的地面没有足够的顶点来置换每颗石子。 |
| 棚内工具台的漆与裸钢 | 底色、切线法线、金属粗糙度 | 靠近后，锈斑边缘、磨痕和反光能否分清。搁板边缘仍沿用原网格。 |
| 备用轮毂的近处轮廓 | 原模型顶点和作者的灰度高度 | 先量灰度范围和顶点间距，再试很小的位移。中灰附近的一张图如果把纯白当作原表面，可能把整件道具都向内推。 |
| 铁丝、草叶和远处的棚顶 | 透明遮罩、轮廓、贴图缩小后的采样 | 镜头移动时检查细线是否断开、草叶是否闪。远景纹理再大也无法显出比屏幕像素更小的刻痕。 |

### 工具台的底色与法线

工具台和场内的另一只货架原先共用一份 GLB，里面的底色、法线和金属粗糙度都是 1024 × 1024。烘焙以同一作者同一模型的 4K 底色和 OpenGL 方向的 4K 法线为输入，生成 2K 候选。模型的顶点、UV 和金属粗糙度沿用原件。这样可以在同一盏灯下看到表面变化，玩家和瓦力碰到的仍是同一个关卡。[原始工具架与下载选项](https://polyhaven.com/a/worn_metal_rack)

一张 4K 图的四个像素会合成 2K 图中的一个像素。对底色，先把 sRGB 解码成线性亮度再求平均。例如两个黑点和两个白点的平均亮度是 0.5，存回 sRGB 大约是 188/255。对法线，每个像素保存一个方向。把红绿蓝还原成三个有正负的分量，求出平均方向，再把向量长度还原成 1。若把这张图也当作颜色调亮，原本平整的面就会朝错误方向反光。[glTF 的材质与法线定义](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#materials)

### 碎石与轮毂的高度

高度图上的明暗表示相对高低。一块碎石地上的黑色污渍可能和浅色石面一样高，因此不能把底色直接转灰度来做高度图。维修站用的是 Amal Kumar 的碎石扫描，原作者给出每次重复覆盖两米。视差沿着观察方向寻找首先挡住视线的那块石面，改变的是该处采样的贴图坐标。轮毂的顶点置换则会移动实际绘制的顶点。当前引擎中的碰撞和静态间接光仍使用导入的网格，制作可站立的高台时，需要另外制作相配的几何和碰撞。[碎石扫描](https://polyhaven.com/a/gravel_road) · [轮毂扫描](https://polyhaven.com/a/rusted_wheel_rim_01) · [LearnOpenGL 的视差示例](https://learnopengl.com/Advanced-Lighting/Parallax-Mapping)

### 复做一份材质

从项目根目录执行下面的命令。需要 Python 3.11 或以上，以及 Pillow 和 NumPy。第一次会把原图放入 `tmp/material-sources`，按作者提供的校验值核对。输出目录保存运行时贴图和 `sources.json`，里面记有原图及产物的指纹。如果手改了产物，复跑会提示另选目录，避免盖掉你的修改。

```powershell
# 工具台的底色、切线法线分别按各自的含义过滤。
python scripts/bake_surface_maps.py scripts/materials/worn-metal-rack.json RedflagForge/assets/game/station/salvage/rack-detail
# 轮毂的 16 位高度先做区域平均，再保存到引擎读取的 8 位通道。
python scripts/bake_surface_maps.py scripts/materials/rusted-wheel-rim.json RedflagForge/assets/game/station/salvage/rim-detail
```

在 Forge 中打开 `Watt-Station.redflag`，从层级选中对应实体，在「模型绘制」查看其材质。外部 Agent 可以使用项目的 `redflag-material-bake` skill，按照同一要求在另一个关卡收集原图、生成候选并提交同机位画面对照。[烘焙代码](../scripts/bake_surface_maps.py) · [Agent 工作方法](../.agents/skills/redflag-material-bake/SKILL.md)

## 站在维修棚里看

开场地面看起来平，先查到的是铺路平面的缩放。它的两个方向放大了一百倍，表示厚度的方向却保留原尺寸，贴图里石面的倾斜也被压平。修正缩放后，正面受光已经能分清碎石。再让相机贴近路肩斜着看，开启视差，石棱后面露出的沟底会变窄。两次调整分开拍，才能看出各自起了多大作用。[出生点原图](../output/validation/station-materials/spawn-original.png) · [同机位调整后](../output/validation/station-materials/spawn-current.png) · [路肩局部对照](../output/validation/station-materials/gravel-normal-current-crop.png)

在工具架前，最值得看的是浅色磨痕的边缘。切换两张 2K 图以后，这个绘制在交错回放里的 GPU 时间中位数为 49.02 微秒，原先是 49.23 微秒。这点差别不足以说明它跑得更快。两张新图按 RGBA8 和完整缩小级别估算，约占 42.67 MiB，远处货架仍使用原先的 1K，因此原图也还要保留。轮毂则只试很小的实际顶点移动，近镜头下最大约半个像素。玩家在正常站位更容易看到的是轮毂边缘落在泥地上、草叶遮住了一部分轮圈。[工具架局部](../output/validation/station-materials/rack-original-current-crop.png) · [轮毂局部](../output/validation/station-materials/rim-original-current-crop.png)
