---
name: redflag-async-performance
description: 评估 Redflag 资源加载、地形构建和 ECS 更新是否值得改成后台任务或分帧协程。适用于需要用实际游戏负载比较耗时、检查线程归属并决定是否启用优化的工作。
---

# 资源加载与 ECS 性能复查

先确定玩家正在等什么。例如，进入维修站时第一帧迟到，和走路时某一帧突然变慢，是两种不同的问题。前者量到资源可以使用或画面可以操作的时刻。后者看同一段移动中的帧耗时分布。记下构建、机器、场景、输入和是否已经读过文件。

**资源加载。** 沿一次实际调用分别计时。以地形为例，`Terrain::Load` 读取并解码 PNG，`BuildChunks` 生成顶点和索引，随后渲染后端创建资源。单独测一段可以定位开销，再把提交、排队、等待和主线程写回计入完整时间。首次创建线程也要计入冷启动；重复构建可以测已运行的线程池。UE 的异步加载接口明确说明完成通知在哪里执行，资源所有者也要持有对象直到回调结束。[UE 资源加载](https://dev.epicgames.com/documentation/en-us/unreal-engine/asynchronous-asset-loading-in-unreal-engine) · [完成回调](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/CoreUObject/FSoftObjectPath/LoadAsync)

**ECS。** 从场景里实际存在的组件数量开始。给候选任务列出读取和写入的组件。地形的几个工作线程可以同时读取同一幅高度图，并分别写自己负责的块。角色移动和相机跟随都涉及角色位置，需要保留本帧的先后顺序。创建实体、删组件或重新排序可能改变迭代器指向的内容。这类改动可以由后台提出，再由持有场景的线程在约定时刻应用。要把复制快照和合并结果的成本也放进比较。[EnTT 多线程说明](https://github.com/skypjack/entt/wiki/Entity-Component-System#multithreading) · [Bevy 系统访问与顺序](https://docs.rs/bevy_ecs/latest/bevy_ecs/system/)

**协程。** 适合“开始加载，下一帧还能处理输入，等资源就绪再显示”的过程。先记下挂起前持有什么，以及恢复时谁还拥有实体。换场景、删除实体、取消请求、旧结果比新结果晚返回，都可以在试玩中重现。同步解码如果仍在主线程执行，协程只会改变它发生在哪一帧。[Unity 协程](https://docs.unity3d.com/Manual/Coroutines.html)

**采用或撤回。** 用同一构建、同一份输入交错运行两条路径，保留原始样本、样本数和中位数、P95。地形逐块核对顶点和索引，ECS 按实体核对输出和事件顺序；游戏轨迹、截图和设备声音能帮助解释差异。耗时变化处在波动范围内、只是把等待移到下一步，或行为发生回退，都保留原路径。记录哪种场景和规模确实受益。项目内的起点是 `tests/run_job_bench.cmd`，原始时间保存在 `output/profiling/jobs/`。
