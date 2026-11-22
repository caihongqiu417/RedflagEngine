# 盐湖城采石坡

这片山坡取自美国地质调查局（USGS）在犹他州盐湖城地区发布的高程数据。Mapzen 把数据分成地图瓦片，项目保存的原件是 `quarry-terrarium.png`。它是 256 × 256 的彩色 PNG，每个像素的三个通道合起来表示当地海拔。下载响应头 `x-amz-meta-x-imagery-sources` 指向 2006 年的 `ned/ned19_n40x75_w112x25_ut_saltlakecity_2006.tif`。[原始瓦片，14/3087/6171](https://s3.amazonaws.com/elevation-tiles-prod/terrarium/14/3087/6171.png) · [Mapzen 数据目录](https://registry.opendata.aws/terrain-tiles/)

从左上角起数，以 0 为第一行、第一列，从第 96 列、第 16 行开始，向右、向下各取 129 个像素。按 `高度（米）= 256 × R + G + B / 256 − 32768` 还原海拔。这一小块最低为 1412.65625 米，最高为 2107.67578125 米。分别对应灰度图的 0 和 65535；中间高度按比例换算，四舍五入成 16 位整数，即引擎读取的 `quarry-height.png`。两张图的校验值见同目录的记录。[Terrarium 格式说明](https://github.com/tilezen/joerd/blob/master/docs/formats.md#terrarium) · [原始参数与 SHA-256](sources.json)

实地相邻采样点约隔 7.26 米。在 Watt Station 里，为了把山坡放在设备区旁，场景将间距设为 0.65 米、总高差设为 24 米。于是同一份轮廓在游戏里占地约 83.2 米见方。山坡表面使用 Amal Kumar 的航拍材质 *Rocky Terrain*。原始扫描宽约 90 米，场景让照片每 42 米重复一次。[关卡配置](../../scenes/Watt-Station.redflag) · [Poly Haven 材质](https://polyhaven.com/a/rocky_terrain) · [原图与校验值](surface-source.json)

3DEP data courtesy of the U.S. Geological Survey. 美国 3DEP（原 NED）高程数据属于公共领域，USGS 请求再分发时标注来源。Mapzen 汇集的各国瓦片各有授权，本页的授权说明对应上面这块美国数据。Poly Haven 的岩地材质由作者以 CC0 1.0 发布。[USGS 数据条目](https://data.usgs.gov/datacatalog/data/USGS%3A40e52270-5c4b-497d-987f-827e374becff) · [Mapzen 署名说明](https://github.com/tilezen/joerd/blob/master/docs/attribution.md#3dep) · [Poly Haven 许可](https://polyhaven.com/license)
