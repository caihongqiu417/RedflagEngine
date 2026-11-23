# 把维修站打成一个资源包

场景里的机器人只引用一份模型。瓦力还需要自己的角色定义，从定义找到行为树和动画状态表。准星图片藏在 HUD 布局的 `Sprites` 下面。如果只复制场景文件，编辑器在开发机上还可能从原目录找到这些文件，换到另一份干净目录就会出现缺图。打包器顺着这些引用查找，把文件放在游戏原来读取的位置，最后给每个文件记录来源、大小和 SHA-256。[依赖收集与写包](../scripts/asset_bundle.py)

以下命令从仓库根目录运行。用于查询资源编号的程序使用引擎自己的 `AssetManager`。项目中的模型和它的 `.rfmeta` 一起移动后，场景里旧的路径仍可以由编号找回来。Python 使用项目 MCP 虚拟环境已有的 YAML 解析库。[原生编号查询](../scripts/AssetIndex.cpp) · [资源登记实现](../Redflag/src/Redflag/Assets/AssetLibrary.cpp)

```powershell
scripts\build_asset_index.cmd
$python = ".\bin-int\redflag-mcp\Scripts\python.exe"
python scripts/stage_windows_runtime.py tmp/release-player
& $python scripts/asset_bundle.py RedflagForge/assets/scenes/Watt-Station.redflag output/packages/Watt-Station.zip --runtime tmp/release-player/RedflagForge.exe
```

解压后双击 `Play.cmd`。它会从清单中读取入口场景，再运行 `.\RedflagForge.exe --vulkan --play assets/scenes/Watt-Station.redflag`。如果只交付场景资源，省略 `--runtime`。加入该参数时会一并复制指定构建的程序、同目录 DLL、Mono 类库、游戏脚本程序集和引擎着色器。玩家电脑还需满足这个构建的 Vulkan 和 Visual C++ 运行要求。游戏需要模型回答或语音时，连接该机上配置的决策与语音服务。

交给另一台 Windows 电脑时，先构建 Release x64 版。上面的暂存命令需要空的输出目录，它会用 Visual Studio 的 `dumpbin` 从 EXE 一层层查到每个 DLL，再从微软发布目录取实际用到的文件。本次检查得到 Mono、Release 版 shaderc，以及 msvcp140、vcruntime140 和 vcruntime140_1。把这五个随包提供，玩家可以直接用 Windows 10/11 和支持 Vulkan 1.3 的显卡驱动运行。Debug 版还需要带 D 后缀的调试库，暂存命令会指出这一点并终止。[本次逐文件依赖和哈希](../output/validation/packaged-watt/release-runtime.json) · [微软 Visual Studio 2022 可分发文件清单](https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution)

带外部贴图的 glTF 会在自己的 JSON 中列出图片和顶点文件。比如 `brick%20wall.png` 会还原成含空格的文件名，嵌在 glTF 内的图像随模型一起打包。材质覆盖、环境图、烘焙探针、HUD 及其引用的图片也会一并收集。3DGS 视角图可由 `--views output/npc-vision/index.json` 单独指定。包内保留索引及图像的目录关系，还会保留同一烘焙目录中的 `scene.ply`。这份项目示例索引记录的是物理工坊；给维修站添加索引时要使用在维修站拍摄的内容。

包根目录的 `asset-manifest.json` 可用来查某张贴图是被谁引用的。遇到缺失文件时，打包器会指出引用者，磁盘上已经生成的包仍可使用。运行 `bin-int\redflag-mcp\Scripts\python.exe tests/check_asset_bundle.py` 可以复查模型移动、外部图片、角色和 HUD 依赖，以及缺图后包文件是否完整。[验证脚本](../tests/check_asset_bundle.py)
