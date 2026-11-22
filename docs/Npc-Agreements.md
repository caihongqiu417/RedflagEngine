# 队友之间的约定

林沅看守补给点，和队友说好了，医疗包留在原地，等队友过来取。沈禾负责跑腿，约好自己把药送过去。两个人都听见「按我们的老规矩处理这份医疗包」。要把这句话变成可执行的动作，游戏需要知道两件事：谁是这个角色的队友，他们之前说定了什么。

在 Forge 的「角色创作 → 角色」打开 `lin.npc.yaml` 或 `shen.npc.yaml`。「与指定队友的约定」填可以直接说给对方听的一句话。场景里「AI 角色」组件的 `Leader` 指向实际队友。角色文件可以被多个关卡使用，这句话会应用于每个实例在当前场景中指定的队友。已有角色没有填写时，读取结果为空。[林沅的角色资产](../RedflagForge/assets/ai/lin.npc.yaml) · [沈禾的角色资产](../RedflagForge/assets/ai/shen.npc.yaml)

玩家发令时，游戏先查角色的背包和队友。两人持有一份药，队友也有背包，因此原地保留和主动递送都可以选。游戏把这两个候选、玩家原话和这名角色的约定交给模型。拿到选择后，行为树开始执行。留药时角色停在原地。送药时角色先走到队友可以接到的位置，再伸手；库存变化发生在动作表标出的接触时刻。[候选与物品检查](../Redflag/src/Redflag/AI/NpcQuery.cpp) · [模型提问与动作执行](../Redflag/src/Redflag/AI/NpcSystem.cpp)

两个示例都关闭了场景观察和角色相机。角色依然知道自己背包里有什么、和指定队友有怎样的约定。这份约定由作者保存在角色资产里。玩家在一局里刚刚转述的线索，仍由该角色的记忆策略决定是否接受和保留。[角色记忆](../Redflag/src/Redflag/AI/NpcMemory.h)

使用 MCP 时，`inspect_npc` 返回 `leader_relationship`。`update_npc_role` 的同名参数可以修改约定；将它设为空字符串可以清除。Forge、MCP 和角色表格都读写 `.npc.yaml` 里的 `LeaderRelationship`。在查看动作选择的预览里，可以先输入同一句话试读；实际给药仍需运行游戏，场景才有真实的背包、距离和接触事件。[MCP 入口](../scripts/redflag_mcp/server.py) · [Forge 角色属性](../RedflagForge/src/NpcAuthoringPanel.cpp)

实测让两人站在离队友四米处，使用同样的候选、库存、动作树与 Qwen3-VL-2B 模型。第一次，林沅留在零米处，等测试把队友移近后才交药；沈禾自己走了两米，在可交接的距离递出药。随后通过 MCP 只交换两份约定，林沅改为走过去，沈禾留在原地。四次交接中，队友的背包每次都只增加一份。另将背包清空时，两种约定下都没有可执行候选，游戏直接拒绝提交。[模型选择、位置与库存](../output/validation/runtime-ai/role-agreements.json)

从仓库根目录运行下列命令。测试在一个临时项目里交换角色约定，使用本机已启动的决策服务。

```powershell
scripts/redflag_mcp/build_npc_tool.cmd
bin-int/redflag-mcp/Scripts/python.exe tests/check_npc_relationship.py
```
