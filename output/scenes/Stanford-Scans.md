# Stanford 扫描模型

三个场景里的主体来自 Stanford University Computer Graphics Laboratory。先在仓库根目录运行 `python tests/fetch_stanford.py`，再打开下表的场景。脚本从原始压缩包中取出指定分辨率的 PLY，并核对压缩包与 PLY 的 SHA-256。模型保存在 Git 忽略的资源目录；每个模型的 `.rfmeta` 和场景一起保存，换一台机器仍能按同一个编号加载。

| 可打开的场景 | 上游重建 | 实际绘制 | 主体可见像素 |
| --- | --- | --- | ---: |
| [Stanford Bunny](../../RedflagForge/assets/scenes/Reference-StanfordBunny.redflag) | bun_zipper_res2.ply | [画面与投影](StanfordBunny.png) · [相机及读回记录](StanfordBunny.yaml) | 96,120 |
| [Stanford Dragon](../../RedflagForge/assets/scenes/Reference-StanfordDragon.redflag) | dragon_vrip_res4.ply | [画面与投影](StanfordDragon.png) · [相机及读回记录](StanfordDragon.yaml) | 65,371 |
| [Happy Buddha](../../RedflagForge/assets/scenes/Reference-HappyBuddha.redflag) | happy_vrip_res4.ply | [画面与投影](HappyBuddha.png) · [相机及读回记录](HappyBuddha.yaml) | 50,652 |

这些 PLY 提供扫描表面。地面、颜色、材质粗糙度、相机和灯光由 Redflag 场景指定。图中的颜色是示例材质的颜色。复现时在根目录运行 `tests\run_scene_gallery.cmd tests\scenes\StanfordBunny.yaml`，将末尾文件名换为另外两个模型名即可。程序保存场景并重新打开它，使用场景主相机以 768 × 512 绘制六帧，从 OpenGL 后处理输出读取图像。表中像素数只统计模型实体，因此地面不会被算进模型的可见面积。

上游允许研究使用和免费镜像，要求保留来源署名；用于商业用途或待售产品须先获许可，学术出版物另有说明。这些扫描数据与项目源码各自遵循其授权。[Stanford 3D Scanning Repository 与使用条款](https://graphics.stanford.edu/data/3Dscanrep/) · [下载地址、成员名与校验值](../../tests/fetch_stanford.py)

## Vulkan 光照与屏幕效果

实验重新打开同一批场景，用主相机和保存的世界变换绘制扫描模型与地面。三张对照图左上角都是逐灯直接光，旁边分别给出 DDGI、ReSTIR DI、ReSTIR GI 的结果。间接光的两格都加回相同的直接光，可以观察阴影一侧的亮度。第二排依次是环境遮蔽、分簇点光、屏幕反射和第八帧 TAA。所有小图采用相同的曝光。打开原图可以对照兔耳边缘、龙身缝隙和佛像脚边的阴影。

| 模型 | 同机位对照 | 模型像素 | ReSTIR DI / GI 复用率 | 屏幕反射命中率 |
| --- | --- | ---: | ---: | ---: |
| 兔 | [原图](stanford-vulkan/StanfordBunny.png) | 13,501 | 68.84% / 63.12% | 12.03% |
| 龙 | [原图](stanford-vulkan/StanfordDragon.png) | 9,192 | 81.27% / 64.43% | 15.23% |
| 佛像 | [原图](stanford-vulkan/HappyBuddha.png) | 7,125 | 80.42% / 47.34% | 8.50% |

表中的复用率统计最终选中邻近像素或上一帧样本的比例，反射命中率统计在屏幕内找到第二个可见表面的比例。两者都以模型和地面的有效像素为分母。佛像脚边能找到反射的表面，朝向空背景的像素则读不到场景外的物体。模型像素数单独按扫描体的材质底色核对。

扫描体直接使用场景指定的底色，光照计算使用场景里保存的点光。这条计算路径支持点光，目前没有接入方向光。分簇点光按场景里的半径 7 计算衰减，与 CPU 对每个有效像素逐灯计算所得的最大 RGB 差值小于 0.000000034。这里只有一盏点光，不能据此判断许多灯光时的速度。反射计算用模型材质的粗糙度 0.63；当前反射接口在整张图上使用这个值，尚未逐像素读取地面的粗糙度。扫描体使用平面几何法线。

八帧 TAA 各取一个稍有偏移的采样位置。用这八张原始帧的平均颜色作为参考，分别计算最后一张单帧和 TAA 输出的 RGB 均方误差。DDGI 和 ReSTIR GI 则与同一场景、每像素追踪 128 次的一次反弹参考图比较。这些误差都在曝光和显示转换之前计算。测试使用 NVIDIA GeForce RTX 5070 Ti Laptop GPU、Debug、288 × 192、固定随机种子 137。图像比较的参数和逐场景数值可在[测量记录](stanford-vulkan/metrics.json)中核对。

用 Python 准备好 numpy、Pillow 和 PyYAML，构建 Debug 引擎库后，在仓库根目录运行 tests\run_stanford_effects.cmd。程序按编号加载模型，回读渲染结果到 tmp/stanford-effects，再生成图与测量记录。[场景加载与 Vulkan 调度](../../tests/StanfordEffects.cpp) · [读回与比较](../../tests/stanford_effects_report.py)

对这三个固定机位，八帧 TAA 与八次直接光采样均值的误差均小于 0.000000000000000003。这里给 TAA 同时传入偏移前的稳定投影，因而相机未动时同一显示像素可以直接积累样本。这个结果只描述相机和模型静止的情形。

每个场景还有一份 RenderDoc 1.47 截帧。三份回放分别找到 12 条计算命令，调试消息均为空。打开任意一帧，事件 8 更新探针、20 读取探针照亮表面。44 写 ReSTIR GI，68 写 ReSTIR DI。80、92、104、128 分别写直接光、环境遮蔽、屏幕反射、分簇点光。逐个读取这四个事件的整张输出，三份截帧都与独立运行中的 GPU 读回逐字节一致。140 写 TAA。截帧保留一轮各效果的实际计算，前表读数另按八帧实验获取。

[兔的帧](../profiling/stanford/StanfordBunny_capture.rdc) · [龙的帧](../profiling/stanford/StanfordDragon_capture.rdc) · [佛像的帧](../profiling/stanford/HappyBuddha_capture.rdc) · [事件、工作组数和数据校验值](stanford-vulkan/events.json)

重录时给同一命令传入 RenderDoc 安装目录里的 DLL，例如 tests\run_stanford_effects.cmd --capture C:\RenderDoc\renderdoc.dll。在仓库根目录用同一版本的 qrenderdoc --python tests/replay_stanford_effects.py 可以重新回放核对。[回放脚本](../../tests/replay_stanford_effects.py)
