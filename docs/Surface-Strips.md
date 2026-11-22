# 沿地面铺设材质

一条泥路通常要在弯道上保持宽度。把矩形贴图拉长，会连里面的石头一起拉长。Forge 的地表带用米保存路线和宽度，还能指定走过多少米重复一次贴图。生成的是普通 glTF 模型，可以像场景中的其他模型一样移动、复制和打包。

在项目的资源目录新建 `Trail.rfstrip`。下面这条碎石小径向前延伸四米，再向左转弯。将 `gravel.png` 放在配置旁边。路径点是地面上的 `[X, Z]` 坐标，一条宽 1.8 米的带以这条线为中心，贴图每两米重复一次。

```json
{
  "points": [[0, 0], [4, 0], [6, 2], [6, 5]],
  "bands": [{"width": 1.8}],
  "subdivisions": 3,
  "repeat_m": 2,
  "color": "gravel.png"
}
```

在 Vulkan Forge 的「项目」窗口进入该目录，双击类型为「地表带」的文件。生成的 `Trail.gltf` 会出现在同一目录，拖入场景就能看见。也可以在项目仓库根目录运行 `python scripts/redflag_mcp/surface_strip.py 路径/Trail.rfstrip`。再次生成保留模型的资源编号。已经打开并持有旧模型的编辑器重新启动后会读取新版。

| 设置 | 实际作用 |
| --- | --- |
| `points`、`subdivisions` | 曲线经过给定的 XZ 点。相邻两点间分成几段由后一个数控制。取 1 时直接连接相邻两点，取较大值时弯道更圆。 |
| `bands[].width`、`offset` | 宽度和离开中心线的距离，都以米计。沿路线前进，正偏移朝右。给两条带相反的偏移就有两道轮迹。 |
| `repeat_m`、`bands[].u` | 前者设为 0 时，整张图纵向恰好铺满路线。后者选图片的横向区间，默认从 0 到 1。例如两道胎印可以各读一张图的半边。 |
| `color`、`normal`、`metallic_roughness` | 都是相对于配置的贴图路径。底色按 sRGB 读取。法线图使用 OpenGL 约定，粗糙度读绿色，金属度读蓝色。 |
| `roughness`、`metallic`、`alpha_cutoff` | 与模型材质面板同义。泥土取金属度 0。可选的透明裁剪比较底色图的透明度，保留裸露的原地面。 |

两个独立项目分别验证单条人行道与双带轮迹，检查生成顶点的宽度和间距、转弯后 UV 的重复长度、重新生成后的资源号和打包文件。从仓库根目录运行 `bin-int/redflag-mcp/Scripts/python.exe tests/check_surface_strip.py`。工具当前生成局部 XZ 平面上的外观网格。起伏地形需另作贴合，物理高度仍由地面或地形的碰撞组件决定。[资源生成代码](../scripts/redflag_mcp/surface_strip.py) · [维修站实拍](../output/validation/station-tracks/README.md)
