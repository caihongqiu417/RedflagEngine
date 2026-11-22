# 从游戏或编辑器调用模型

给道具写一句说明，只需要一道问题和一段回答。打开 Vulkan 编辑器的「模型测试」，输入“请为维修站的柜门钥匙写一句物品说明”，保留默认模型并发送。结果可以复制出来，在采用前修改文字。这个操作在空场景里也能使用。勾选「从场景物体中选择」时，编辑器会把当前物体的编号和名称一起发送，收到的结果则是一个物体编号。[编辑器入口](../RedflagForge/src/DecisionPanel.cpp)

## C# 游戏脚本

下面的方法可以直接放在自己的工具类中调用。它只引用 `Redflag-ScriptCore.dll`。同一个连接可以处理多轮请求，游戏关闭、窗口销毁或项目切换时，用传入的取消标记撤回还在等待的任务。已有关闭按钮时，可由按钮调用 `CancellationTokenSource.Cancel()`。[模型连接](../Redflag-ScriptCore/Source/AI/ModelConnection.cs) · [可编译的调用示例](../tests/ModelConnectionTests.cs)

```csharp
// 示例，接口定义在 Redflag-ScriptCore/Source/AI/ModelConnection.cs。
using Redflag;
using System.Threading;
using System.Threading.Tasks;

static class ItemWriter
{
    public static async Task<string> Describe(string name, CancellationToken cancel)
    {
        using (var model = new ModelConnection())
        {
            // name 来自游戏输入框，也可以换成编辑器选中的物品名称。
            return await model.ReplyAsync("为" + name + "写一句物品说明。",
                instructions: "用中文写，保留物品名称。", cancel: cancel);
        }
    }
}
```

玩家点击按钮时，把返回的 `Task<string>` 存在脚本里。每帧的 `OnUpdate` 先看 `IsCompleted`。如果仍在等待，游戏可以照常绘制下一帧。任务完成后再读结果、更新文字框或提交编辑器撤销操作。用 `IsCanceled` 和 `IsFaulted` 分别显示取消和错误。主动取消会结束这次任务。等候满一分钟仍未拿到回答，客户端会给出 `TimeoutException`；单次连接或读取本机网关超过四秒也会超时。这样场景修改都发生在游戏线程。

需要识图时，调用者给图片起一个名称，把整张 PNG 或 JPEG 文件的字节交给 `ModelImage`。来源可以是截图、导出的相册或离线重建的视角图。下面的示例会把一张图片和问题放入同一轮请求。[图片定义](../Redflag-ScriptCore/Source/AI/ModelImage.cs)

```csharp
// 示例，文件画面可换成自己的场景截图。
byte[] png = System.IO.File.ReadAllBytes("workshop.png");
var photo = new ModelImage("维修间", "image/png", png);
string text = await model.ReplyAsync("图里的小箱子是什么颜色？",
    images: new[] { photo }, cancel: cancel);
```

模型写出的文字由脚本决定用途。若要移动角色或取物，请把游戏允许的动作组织成候选，再用 C++ 的 `DecisionClient::Start` 取得候选编号。角色执行之前还要检查目标当前是否存在、是否够得到。同一接口也提供 `StartReply` 生成自由文本。两种结果分别存放在 `Choice` 和 `Text`。[C++ 客户端](../Redflag/src/Redflag/AI/DecisionClient.h) · [游戏动作](AI-Runtime.md)

## 本机接口

从项目根目录运行 `scripts/Start-NpcServices.ps1`。脚本能复用已经启动的模型，也可通过 `-ModelEndpoint`、`-ModelName` 和 `-Vision` 接入配置好的上游。本机入口默认监听 `127.0.0.1:8716`，C# 构造函数和 C++ 构造函数都可以指定其他端口。密钥需要时由网关进程从 `REDFLAG_MODEL_TOKEN` 读取。语音使用另一个本机端口 8717。[启动与配置](../scripts/Start-NpcServices.ps1) · [语音重放与实际记录](../output/validation/runtime-ai/README.md)

| 请求 | 输入 | 结果 |
| --- | --- | --- |
| `GET /v1/health` | 无 | 默认模型、允许使用的模型列表、图片能力和 `capabilities` |
| `POST /v1/replies` | 必填 `prompt`；可传 `instructions`、`model`、`images` | HTTP 202 与本轮 `id` |
| `POST /v1/decisions` | 同上，另需 `candidates`；可传 `observations` | HTTP 202 与本轮 `id` |
| `GET /v1/replies/{id}` | 创建时返回的编号 | `pending`，或 `completed` 与 `text`，或 `failed` 与 `error` |
| `GET /v1/decisions/{id}` | 创建时返回的编号 | 同上，完成时返回 `choice` |
| `DELETE /v1/replies/{id}`、`DELETE /v1/decisions/{id}` | 相应接口的编号 | 撤回等待或归还结果，再读返回 HTTP 404 |

每个候选和观察都包含两个字符串，`id` 供代码比较，`description` 给模型阅读。一张图片用 `id`、`mime`、`data` 表示，其中 `data` 是整个文件的 Base64。请求体连同图片编码最多 12 MiB。客户端取完结果会主动归还编号，服务进程重启后所有旧编号失效。

如果不支持图片，发送时会收到 `images_unsupported`；换成支持图片的模型或只保留文字即可。模型名拼错会收到 `model_unavailable`，允许的名称可以在健康接口查询。推理阶段出现 `context_too_large` 时，可缩短问题和观察，或增加上游模型的上下文容量。`provider_busy` 表示上游繁忙，`provider_unavailable` 表示上游当前不可达，`provider_unauthorized` 要检查网关进程的凭证。调用方可显示原因，再由使用者决定是否重试。游戏没有提供的动作编号会被拒绝并返回 `invalid_choice`。[协议与异常恢复测试](../tests/check_ai_service_contract.py)

## 空项目复测

如果只下载了独立发布包，在解压目录运行 README 中的 ModelConsole 命令。有 Redflag 源码时，从仓库根目录运行 `python tests/check_model_connection.py`，默认使用已启动的本机 Qwen3-VL-2B，端口为 18157。测试会编译一份完整的 ScriptCore 和一个没有场景的 C# 控制台程序，分别用 Windows 的 .NET Framework 和项目自带的 Mono 6.12 运行。两次均生成了钥匙的中文说明，看图回答「红色」，取消旧问题后又成功返回「木」。测试另外关闭本机网关，核对调用者确实收到连接失败。准备环境、模型权重和本机语音的步骤见复测记录。[C# 实际回答](../output/validation/runtime-ai/csharp-sdk.json) · [复测说明](../output/validation/runtime-ai/README.md)

发布引擎时，请保留 `mono/lib` 下的程序集和 `mono/etc/mono/4.5/machine.config`。Mono 从这份配置找到 HTTP 请求处理器。工程中采用 Mono 6.12 原版配置，SHA-256 为 `ee950004b576fb28dc85f4b0435ed04bf96612de2e8b53be84d07afe85a0de6c`。[Mono 6.12.0.182 的原始配置](https://github.com/mono/mono/blob/mono-6.12.0.182/data/net_4_5/machine.config)
