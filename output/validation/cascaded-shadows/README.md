# 太阳下的支墩与悬板

游戏主相机固定在 (0, 3.2, 8.4)。保持灯光、相机和三个物体的位置，分别关闭投影、开启硬阴影、把太阳角半径调到 4°。支墩下缘贴着影子，悬板下方留出了距离。图片保留了 1920 × 1080 的原输出，只开启直接光，用纯黑表示完全挡住阳光。[关闭投影](sun-off.png) · [硬阴影](hard.png) · [软阴影](soft.png)

斜照平地时，相邻阴影像素本来就有不同深度。第一版把它们直接和中心深度比较，平地也会出现条纹。修正后，在画面下方无物体遮挡的一块平地取 248,832 个像素，和相同光照、关闭投影的那一帧逐一比较。线性 RGB 任一通道差超过 0.02 的像素比例从 62.645% 降至 0。支墩、悬板和立柱的实际影子仍会让 32,777 个像素的差超过 0.02。[修正前的实机图](before.png) · [旧版截帧](before.json) · [新版截帧](game.json)

独立绘制测试使用从头顶照下的太阳，逐点记录三层距离里的地面能见到多少阳光。中间的挡板离地 8 米，整块位于相机上边缘之外。在它旁边每隔 7.8125 毫米量一次，从 0° 改为 4° 时，处于 5% 到 95% 之间的点从 6 个增至 143 个。近、远两条扫描线也保留在 CSV。完整的小场景帧有 9 个网格绘制、3 个保存深度的计算和 1 个地面可见度读取。测试还换用了斜照平地的太阳，检查 771 个无遮挡点全都能收到阳光。[真实 Vulkan 截帧](Shadow.rdc) · [三条扫描线](profiles.csv) · [GPU 事件与计时](three-maps-time.json)

硬、软阴影的三张深度图已经在游戏第 8 帧之前生成，直接光事件 66 的 GPU 时间中位数分别为 0.304 和 0.340 毫秒，关闭投影时为 0.287 毫秒。CPU 夹具分开计了记录几何命令以及整个更新调用含等待的时间。硬件为 NVIDIA GeForce RTX 5070 Ti Laptop GPU，使用 Debug、RenderDoc 1.47。[硬阴影的 11 次回放](hard-time.json) · [软阴影](soft-time.json) · [关闭投影](sun-off-time.json) · [CPU 记录](cpu.csv)

在仓库根目录先生成并构建 Debug 的 Redflag、RedflagForge。前两条命令分别保留独立 Vulkan 帧和 CPU 耗时，第三条通过正式游戏复拍。DLL 路径可以替换为本机 RenderDoc 的安装目录。

~~~powershell
.\tests\run_cascade_tests.cmd output/validation/cascaded-shadows D:\Cdev\Redflag\tmp\tools\renderdoc\RenderDoc_1.47_64\renderdoc.dll
.\tests\run_cascade_tests.cmd output/validation/cascaded-shadows --profile
python tests/capture_cascade_shadows.py --time
~~~

场景菜单的“级联阴影 CSM”配置层数、分辨率、最远距离和太阳角半径，选 0 层即可回到原来的投影方式。级联只替代 Vulkan 画面中的方向光直接阴影。离开配置的距离后不再查询级联，探针、反射和雾仍有各自的场景遮挡查询。当前按相机深度直接选择一层；远处纹素更宽，跨层时仍可能看见精细程度变化。OpenGL 的单张方向光阴影同样支持 PCF/PCSS。
