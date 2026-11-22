# 从接口查到一次修改

例如你想调整创建网格的方式。模型把顶点和索引交给哪个接口，两个后端都要在哪儿改？在仓库根目录运行下面的第一条命令。返回的第一项是 `Redflag::RenderDevice::CreateMesh`，带着函数签名和参数说明。第二条会找到模型调用它的代码、OpenGL 和 Vulkan 接手的地方，还有使用同名方法的测试。第三条读取当前源码的一小段，可以把示例行号换成刚查到的位置。

```powershell
python scripts/dev_context.py find "顶点和索引创建网格"
python scripts/dev_context.py show "RenderDevice::CreateMesh" --limit 2
python scripts/dev_context.py show "Redflag/src/Redflag/Renderer/Mesh.cpp:25"
```

命令行与 MCP 共用离线 API 手册里的声明。返回的 `source` 是相对于引擎仓库的文件和行号。查到重载时用行号选择。实现和测试里的文字来自当前工作目录，方法同名或经虚函数调用时，点开原行才能确认运行时选择了哪个对象。测试脚本引用了某个源文件，也只说明它会编译或使用该文件。是否真的覆盖这次改动，仍要读断言并运行相应测试。头文件在手册发布后改过，详情会提示重新生成索引。[手册生成命令](README.md#更新索引)

## 留下一次可复查的修改

在动手前，把准备修改的文件交给 `start`。它保存你当下看到的版本，因此文件里原先就有的手工修改，会出现在比较的起点。下面以调度器为例。PowerShell 变量保存这次的任务编号，后续命令都指向同一份记录。

```powershell
$task = (python scripts/dev_context.py start "核对主线程执行待办任务的顺序" --files Redflag/src/Redflag/Core/JobSystem.cpp | ConvertFrom-Json).task
python scripts/dev_context.py show JobSystem::Pump --task $task
# 在编辑器里完成修改，然后实际运行测试。
python scripts/dev_context.py verify $task -- cmd /d /c tests\run_job_bench.cmd --verify
python scripts/dev_context.py review $task
python scripts/dev_context.py note $task "请补一次依赖任务失败时的检查。"
```

`review` 显示哪些选定文件发生变化、AI 查到了哪些资料、测试命令和退出码。点开 `test.log` 能看编译器或测试程序当时的输出，旁边的 `changes.diff` 是相对于任务起点的完整文本差异。改完代码再看旧测试，`current` 会告诉你当时受跟踪的文件是否还与现在一致；退出码单独保留，失败尝试也在记录里。任务记录能说明文件在这段时间发生了什么，两个人同时修改同一个文件时，逐行署名仍要看版本管理。

查询记录中的 `sources` 把每个文件名对应到当时内容的 SHA-256 指纹。在该任务的 `sources` 文件夹里可以找到原始字节。普通查询默认给四项；最近八次查询、验证或批注会直接显示，更早的记录仍在 `evidence` 指向的目录。检索外部副本可在子命令前加 `--root`，另一个游戏项目可通过 `--project` 或 `REDFLAG_PROJECT` 选择。

外部工具调用 `lookup_dev_context` 搜索或查看详情，`begin_dev_task` 记下修改起点，`review_dev_task` 读取结果，`comment_dev_task` 留下意见。本机命令行的 `verify` 负责启动开发者选定的测试，MCP 可以直接读取同一份回执。[命令入口](../../scripts/dev_context.py) · [MCP 服务](../../scripts/redflag_mcp/server.py) · [入口一致性测试](../../tests/check_dev_context.py)
