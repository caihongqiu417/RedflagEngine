# 把创作图像放进自己的游戏

维修站的任务图标是一张 128 × 128 的透明 PNG。窗口打开时，HUD 读取图片，脚本只告诉界面当前任务。制作这枚图标时用了什么提示词、哪张大图是原件，保存在另一份来源文件里。换成做种植游戏，可以沿用相同的工具，把种子袋放进新项目的背包图集。[维修站图标](../RedflagForge/assets/ui/station/ServiceTicket.png) · [原图尺寸、哈希与完整提示词](../RedflagForge/assets/ui/station/ServiceTicket.png.rfsource.yaml)

## 导入自己的 PNG

先在引擎仓库根目录运行 `scripts\build_asset_index.cmd`。这个小程序调用引擎本身的 `AssetLibrary`，第一次导入时给图片编号。再按 MCP 文档安装 Python 依赖。即使不连接任何生成模型，也可以导入画师交付的 PNG。假设你的项目在 `D:\Games\Garden`，里面有 `assets` 目录，在引擎仓库根目录运行下面的 PowerShell 命令。[MCP 运行环境](../scripts/redflag_mcp/README.md) · [引擎的登记程序](../scripts/AssetIndex.cpp)

```powershell
# 在 Garden 的 inventory 图集中登记背包图标，源文件可以放在项目外。
$python = ".\bin-int\redflag-mcp\Scripts\python.exe"
& $python scripts/redflag_mcp/image_assets.py --project D:\Games\Garden import inventory SeedBag.png D:\Art\seed.png --creator "游戏美术组"
```

此时 `Garden/assets/ui/inventory` 下有三个文件。界面使用 `SeedBag.png`，`SeedBag.png.rfmeta` 保存该项目里稳定的贴图编号，`SeedBag.png.rfsource.yaml` 保存来源、像素尺寸和内容的 SHA-256 校验值。下次导入同名修订稿，编号继续沿用。交付的 PNG 若是裁过的小图，还可以传 `--original` 指向原始大图，用 `--processing` 写下裁剪或缩放操作。美术组自己的图无需填生成模型或提示词。[导入与文件替换](../scripts/redflag_mcp/image_assets.py)

## 让配置好的端点生成图片

工具接受与 Images API 兼容的生成端点。将完整 POST 地址写入 `REDFLAG_IMAGE_ENDPOINT`，用 `REDFLAG_IMAGE_MODEL` 指定服务端已部署的模型。需要鉴权时，从环境变量 `REDFLAG_IMAGE_KEY` 取令牌。请求中包含 `model`、`prompt` 和数量 `n=1`，服务返回 `data[0].b64_json`，内容必须是 PNG 的 Base64 编码。来源文件记录主机名、模型和原提示词；服务若返回修改过的提示词，也会一并保存。令牌不会出现在来源文件中。[OpenAI Images 接口的返回格式](https://developers.openai.com/api/reference/resources/images)

```powershell
# 已在当前终端配置以上环境变量。生成后仍使用同一 PNG/编号/来源格式。
& $python scripts/redflag_mcp/image_assets.py --project D:\Games\Garden generate inventory SeedBag.png --prompt "一只打开的牛皮纸种子袋，透明背景，适合背包图标"
```

如果通过 agent 操作，在启动 MCP 时把 `REDFLAG_PROJECT` 指向 Garden，先查看 `get_capabilities` 返回的项目路径。告诉 agent “将 D:\Art\seed.png 导入 inventory 图集，名字是 SeedBag.png”，它可调用 `import_ui_image`。说“用已配置的生图服务制作图标”时才需调用 `generate_ui_image`。这两个入口和上面的命令共用保存代码。图片或来源不正确，或目标文件暂时无法替换时，先前的图仍可继续使用。[MCP 工具入口](../scripts/redflag_mcp/server.py)

维修站图标的原图来自本次会话实际调用的 Codex 内置 ImageGen，模型版本未公开。原图为 1254 × 1254，历经去透明边、缩小、居中补到 128 × 128。用原始 PNG 和相同的 ImageMagick 命令重做，四个颜色通道逐像素一致。HTTP 生成路径另外用本机协议替身验证了返回 PNG、503、损坏图像与文件占用等情况；测试没有调用收费服务。可在引擎仓库根目录运行 `.\bin-int\redflag-mcp\Scripts\python.exe tests/check_image_assets.py`，它会新建两个临时独立游戏项目，用真实 MCP 客户端重新验证。[两项目回归](../tests/check_image_assets.py)
