# 地形的高度、网格与碰撞

地形从一幅灰度 PNG 开始。把它想成测量员铺在地上的方格纸，每个交点记下一处地面高度。相邻交点隔多远，由 `TerrainComponent.CellSize` 决定。如果间距是 1 米，一幅宽 5 点、深 4 点的图实际覆盖 4 米 × 3 米。横向的每一格都需要左右两个端点，所以 5 个点只有 4 格。第一个像素位于模型坐标原点，图片向右是 +X，向下是 +Z。把整个地形实体平移、旋转或缩放后，网格与碰撞一起变换。[地形资源接口](../Redflag/src/Redflag/Terrain/Terrain.h) · [组件配置](../Redflag/src/Redflag/Terrain/TerrainComponent.h)

高度图可以保存 8 位或 16 位的单通道数据。16 位图中，0 表示黑色，65535 表示白色。如果把 `HeightScale` 设为 3 米，黑色位置的高度就是 0 米，白色则是 3 米。项目用同一套计算读取两种图片，把 8 位像素值乘以 257 就能落到同一个范围，比如 255 × 257 得到 65535。画面的材质颜色另取自 `BaseColorTexture`，它的颜色、纹理重复次数都不会改变地面高度。[高度图读取与网格生成](../Redflag/src/Redflag/Terrain/Terrain.cpp)

## 两个采样点之间的地面

测试图左上角的一格有四次测量。第一行由左至右为 0 和 10000，第二行为 3000 和 40000。程序从左上到右下连一条对角线，画出两个三角形。在这格里向右走 0.75 米、向下走 0.25 米，会站在包含左上、右上、右下三个点的那一片平面上。该位置到三个顶点的插值权重依次是 0.25、0.50、0.25。因此高度图上的读数为 15000；换算为米是 $3\times15000/65535\approx0.687$。沿另一条路线，向右走 0.25 米再向下走 0.75 米，脚下用的是左上、左下和右下三个点，得到的高度约为 0.526 米。[5 × 4 的 16 位测试图](../tests/fixtures/terrain/height16.png)

游戏脚本若只需得到这个位置的高度，可使用 `Terrain::Sample`。需要知道坡朝哪边倾斜时，`SampleSurface` 还会返回三角面的单位法线。平地的法线是 $(0,1,0)$；倾斜后，其 Y 分量会变小。两者都接受模型坐标，越过地图最外圈就返回空。对于已经放进场景、可能转过角度的地形，可以从角色脚下沿世界坐标的竖直方向调用 `Scene::Raycast`，直接得到世界坐标下的交点和法线。[场景查询接口](../Redflag/src/Redflag/Scene/Scene.h)

```cpp
// Redflag/src/Redflag/Terrain/Terrain.cpp，调用 Terrain::SampleSurface 的例子。
Ref<Terrain> heights = terrain.GetTerrain();
if (heights)
{
    const auto ground = heights->SampleSurface(.75f, .25f, terrain.CellSize, terrain.HeightScale);
    if (ground)
        foot.y = ground->Height;  // foot 和采样点在同一地形的模型坐标中。
}
```

## 分块与接缝

`ChunkCells` 表示一块地形每边最多包含多少个格子。刚才那张图若每边取两格，会得到四块。最下面只剩一行格子，就按实际尺寸建块。每块都能单独建立顶点和索引。相邻块各自保留相接的那一排顶点，坐标取自原图的同一列。如果只在各自边界内求坡度，同一条接缝会有两种光照。这里对整幅原图的前后、左右相邻点求坡度，两边由此得到相同的顶点法线和纹理坐标。着色时使用这些顶点法线来表现连续的山坡；脚底射线仍取实际三角面的法线。[分块实现](../Redflag/src/Redflag/Terrain/Terrain.cpp) · [网格缓存](../Redflag/src/Redflag/Terrain/TerrainMeshes.cpp)

这一版内存中保留整幅高度图，所有块采用同一采样间距。LOD 会根据观察距离改变几何密度，相邻块密度不同时还要处理接缝。大范围地形的纹理可以分成许多页，按需要读入附近或画面上正在使用的页，这称为虚拟纹理。分块、LOD 和纹理流送分别处理不同的开销。GAMES104 的地形课程适合接着学习这些概念。《三角洲行动》在 GDC 2025 的讲稿 “Proceduralism: Terrain & Road” 中展示了不同平台的地形网格间距与虚拟纹理开关，也用投射到地面的方法查找悬空的植被。这些资料可用来设计后续实验。[GAMES104 课程目录](https://games104.boomingtech.com/sc/course-list/) · [GDC 原讲稿](https://media.gdcvault.com/gdc2025/Slides/Jiao_Hang_Delta%2BForce%2BPerformant.pdf)

## 箱子落到地面

开始模拟后，场景用同一幅高度图创建一个静态 PhysX 高度场。图中无符号整数的范围是 0 至 65535，PhysX 接收有符号的 16 位整数。写入时先减去 32768，再把碰撞形状垂直抬高相应的米数，黑色位置就仍在模型坐标的零高度。碰撞格沿用网格的对角线。这样角色走到格子的中间、石子落在斜坡上时，脚下撞到的三角形才是引擎画出来的那一片。[物理高度场的创建](../Redflag/src/Redflag/Physics/PhysicsWorld.cpp) · [NVIDIA PhysX 5.6.1 高度场文档](https://nvidia-omniverse.github.io/PhysX/physx/5.6.1/docs/Geometry.html#height-fields)

编辑场景和运行场景可以共享只读高度数据，各自的物理世界持有自己的刚体。修改位置、缩放或高度范围，下一次物理更新会换成新形状。关闭碰撞后，箱子会继续下落，删除地形组件也会立即移除对应的刚体。需要在同一路径重新导入编辑过的高度图时，调用 `TerrainComponent::Reload()`；下一次读取会重新加载它。[场景同步与释放](../Redflag/src/Redflag/Scene/Scene.cpp)

在仓库根目录先运行 `.\scripts\BuildPhysX.ps1 -Configuration Debug` 安装含 Cooking 的 PhysX，再构建 Debug x64 的 Redflag 项目。运行 `tests\run_scene_tests.cmd --terrain` 可复查上面的例子。测试比较 12 个格子里全部 24 个三角形的几何高度、法线和 PhysX 射线；还验证了动态箱子落地、关闭地形后下落、旋转与非等比缩放，以及移除组件后的查询。[完整验证代码](../tests/TerrainTests.h)

同一条测试还会打开实测地形。盐湖城采石坡有 129 × 129 个高度样点，每块 32 × 32 格，共 16 块、32768 个三角形。按原地貌的采样间距与高差，从上方向 64 个位置发射线，再和地形接口返回的高度比较。这次 Debug x64 检查中，最大垂直差约为 0.061 毫米；这个数衡量的是两段代码算出的地表彼此相差多少。原始测绘精度由 USGS 数据决定。游戏场景缩放过这片山坡，原图、换算与场景尺寸可以在资产目录中对照。[采石坡的来源与裁剪](../RedflagForge/assets/terrain/quarry/README.md)

保存关卡时，地形组件记录两条路径。一条指向决定地表起伏的高度图，另一条指向显示岩地颜色的照片。在仓库根目录先运行 `scripts\build_asset_index.cmd` 和上面的地形测试，再用装有 `ruamel.yaml` 的 Python 运行 `python tests/check_quarry_bundle.py`。它从正式 Watt Station 打包，读取包里的关卡，将两张原图解到空目录，再让引擎在该目录读写包中的地形实体。测试逐项比较重开后的八个地形字段，另外核对了两张图的字节内容及随包的署名说明。[打包与重开验证](../tests/check_quarry_bundle.py)
