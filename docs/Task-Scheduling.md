# 地形加载与游戏线程

进入维修站时，引擎先解码一幅 129 × 129 像素的高程图，再把相邻采样点连成可以绘制的三角形。不同地块读取同一幅图，各自写入自己的顶点和索引。这里可以让几个 CPU 核心同时工作。组装顶点之后，游戏线程继续创建网格。角色、相机与渲染器仍然看到原先顺序的地块。

调度器把一次工作表示为 `Job`。`JobSystem::Submit` 接收工作内容、执行线程和需要先完成的任务。地形分成几组交给 `Worker`，各组完成后再继续使用网格。需要访问场景的后续工作交给 `Main`，宿主在游戏循环中调用 `Pump` 来执行。读取文件失败时，依赖这次读取的写回也会失败；调用 `Wait` 的地方能取得原来的异常。[调度器接口](../Redflag/src/Redflag/Core/JobSystem.h) · [地形网格](../Redflag/src/Redflag/Terrain/Terrain.cpp)

编辑器里换贴图时，还要考虑先后次序。假设先选 A，还没读完又选了 B。`AssetUpdate::Request` 给两次选择分配不同版本，允许后台各自读取。主线程准备发布时再核对版本，较晚完成的 A 就无法覆盖 B。场景被关闭后，写回函数通过弱引用发现原来的所有者已销毁，会直接结束。上一个可用资源由引用计数管理，直到显示它的最后一个对象也释放引用。[资源更新](../Redflag/src/Redflag/Assets/AssetUpdate.h) · [UE 异步资源加载](https://dev.epicgames.com/documentation/en-us/unreal-engine/asynchronous-asset-loading-in-unreal-engine)

## 同一份数据跑两遍

下面量的是一项 CPU 工作从开始到结果可用的时间，单位为微秒。Windows x64 的 MSVC 优化构建在同一台机器上交替运行串行与并行版本，每组有 60 对样本。P95 表示排序后约 95% 的样本落在这个时长以内。地形按字节核对顶点并逐项比较索引，场景变换按实体对照矩阵。[机器、资产哈希与统计](../output/profiling/jobs/summary.json) · [测试代码](../tests/TerrainJobBench.cpp)

| 维修站负载 | 串行中位数 | 后台方案中位数 | 串行 P95 | 后台方案 P95 |
| --- | ---: | ---: | ---: | ---: |
| 从已解码高度图重建地形，每块 32 格，复用四个工作线程 | 526.5 | 307.8 | 803.2 | 455.8 |
| 计算场景中 73 个实体的变换矩阵 | 8.5 | 34.3 | 11.6 | 66.7 |
| 读取并解码一张图，提交后立刻等待 | 287.85 | 321.35 | 525 | 528 |
| 首次建立线程池，直到地形准备好，文件已在系统缓存中 | 482.45 | 508.95 | 831.4 | 793.3 |

地形重建这一组，60 对样本中有 57 对并行执行更快。项目因此在已经有工作线程、地形达到测量规模且使用小地块时启用这条路径。73 个变换矩阵全部在一个线程里算完只需约 8.5 微秒，排队和等待反而比计算花得久。这个场景继续按原顺序更新实体。只为首次加载一张高度图启动线程池也没有稳定收益。[采用条件](../Redflag/src/Redflag/Terrain/TerrainMeshes.cpp) · [EnTT 多线程说明](https://github.com/skypjack/entt/wiki/Entity-Component-System#multithreading)

协程可以把操作分成几帧，例如这一帧发出加载请求，后面某一帧再显示模型。一个耗时的解码函数如果依然在游戏线程上执行，在它结束之前，下一帧仍然要等。判断能否改善手感，需要另外量持续游玩时的帧耗时。[Unity 协程手册](https://docs.unity3d.com/Manual/Coroutines.html) · [项目性能复查方法](../.agents/skills/redflag-async-performance/SKILL.md)

## 在游戏脚本里等待

假设门打开半秒后，角色要显示墙上的留言。脚本可以把“等开门”“给门留半秒钟”“从文件里读出留言”写在同一个方法里。每次写到 `yield return`，游戏先去做这一帧其余的工作。条件满足时，脚本在游戏线程从下一行继续。这段代码可以放在自己的 Entity 子类里。调用 `OnDoorOpened` 的是游戏里负责开门的交互脚本。[游戏脚本入口](../Redflag-ScriptCore/Source/Scene/Entity.Coroutines.cs) · [完整可运行的脚本](../tests/CoroutineProbe.cs)

```csharp
// 示例文件 DoorNotice.cs，需要 using System.Collections.Generic 和 using Redflag。
private readonly GameSignal door = new GameSignal();
private Coroutine notice;

void OnCreate() { notice = StartCoroutine(ReadNotice()); }
public void OnDoorOpened() { door.Raise(); }
public void SkipNotice() { notice.Cancel(); }

IEnumerator<GameWait> ReadNotice()
{
    yield return door.Next();               // 从这一行起，等待下一次开门。
    yield return GameWait.Seconds(0.5f);   // 按游戏经过的秒数计时，暂停游戏也会暂停等待。
    TextAssetRead file = new TextAssetRead("notes/door.txt");
    yield return file;                     // 从项目 assets/notes/door.txt 读取 UTF-8 文本。
    System.Console.WriteLine(file.Error ?? file.Text);
}
```

同一扇门可以有多个等候者，它们共享同一个 `GameSignal`。已经开过的门如果需要记住当前状态，应保存门的开关状态，并用 `GameWait.Until(() => isOpen)` 读取它。只想让动态内容在下一帧出现，可以直接 `yield return null`。每次更新只执行协程中的一个步骤，游戏线程在实体的 `OnUpdate` 之前恢复该实体已启动的协程。脚本可以不定义 `OnUpdate`。[通知](../Redflag-ScriptCore/Source/Scene/GameSignal.cs) · [等待条件](../Redflag-ScriptCore/Source/Scene/GameWait.cs)

已有的网络请求通常返回 `Task<T>`。用 `yield return GameWait.For(request)` 等它完成，再检查 `IsCanceled` 或 `IsFaulted`，成功时读 `request.Result`。取消协程会停止后面的脚本步骤，并执行迭代器里的 `finally`。要同时终止 HTTP 请求，应在 `finally` 里取消发起请求时传入的 `CancellationTokenSource`。如果脚本自身抛出异常，可以从协程句柄的 `Error` 看到原因，其他实体继续更新。[协程的结束与取消](../Redflag-ScriptCore/Source/Scene/Coroutine.cs) · [模型请求的取消参数](../Redflag-ScriptCore/Source/AI/ModelConnection.cs)

读取留言时，后台任务记录为 `Read script text asset`；游戏线程随后执行 `Script text ready`，依赖关系能在 Forge 的任务时间线上看到。读文件期间删掉角色，角色的协程会结束，完成的读取也不再交给原脚本。测试用每步四分之一秒的场景分别等待一帧、半秒、开门事件和一个结果为 17 的任务，还检查文件丢失、请求取消以及场景关闭。在仓库根目录运行 `python tests/check_coroutines.py` 即可复测。[文件读取接口](../Redflag-ScriptCore/Source/Scene/TextAssetRead.cs) · [工作线程与主线程回调](../Redflag/src/Redflag/Scripting/CoroutineBindings.cpp) · [本次实测](../output/validation/coroutines/check.json)
