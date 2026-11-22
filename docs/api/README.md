# Redflag API 手册

双击[离线手册](../../output/api/index.html)即可查看入门示例和公共声明。先在“接口索引”输入名称，例如 DrawIndexed，再按模块或声明类型缩小范围。点击文件名可查看带行号的源码；把 HTML 单独带走时，内嵌的源码仍可打开。

给自己的场景添加能接令的队友，可以先跟着[AI 角色接入指南](../AI-Runtime.md)运行维修员和电梯的示例。

想让角色站在自己导入的高度图上，可以从[地形采样、分块与碰撞](../Terrain-Heights.md)里的小坡面开始。

## 更新索引

在仓库根目录用 Python 3.10 或更新版本执行以下 PowerShell 命令。

    py -3 -m venv tmp/api-tools
    .\tmp\api-tools\Scripts\python.exe -m pip install -r docs/api/requirements.txt
    .\tmp\api-tools\Scripts\python.exe docs/api/generate.py

生成器从 Redflag/src、RedflagForge/src 的 C++ 头文件和 Redflag-ScriptCore/Source 的 C# 源文件读取语法树，每个声明保留签名、所属类型和行号。模板、重载、枚举值和公开字段分别列出。具体平台的类型按目录分类，是否从编辑器调用请查对应实现与测试。

生成器按源文件列出条件编译分支，展开本项目的事件声明宏。第三方库、C++ 实现文件里的局部类型以及 C# 内部调用不在公开索引中。手写示例和布局维护在 reference.html。修改接口或示例后重新生成，提交生成器、模板和最新版 HTML。

工具可以把改动步骤、回执和截图发布到编辑器。试玩中还可以交接操作权或按帧给某个场景物体写意见。[Forge 创作审查](../Creation-Review.md)
