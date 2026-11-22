# 我用 Agent 开发 Redflag 的经验

Redflag 的 API 手册有一个搜索框。输入 `AskNpcCandidate`，能找到让 NPC 从照片里寻找目标的接口。做游戏时，我还得点开这条结果，看看函数声明，才能弄清它接受什么参数。如果我只请 AI “测一下搜索”，它数一数页面上有几条结果，就可能告诉我功能正常。所以我会把要求说到最后一步：点开源码，读出函数声明，再关掉弹窗，看看能不能接着查。

我挑开发工具时，通常就从这样一件手边的小事开始。页面点起来顺不顺，需要打开浏览器。代码在别人机器上报错，则要找到那一次运行留下的日志。同样是让 AI 帮我查问题，给它看的东西会很不一样。下面的网页检查有实测，两段纠错对话则用假设的情境来讲。

## 在浏览器里走完一次操作

这次整理笔记时，我让 agent 用 Playwright 的 Python 接口打开 Chrome，照着这几个动作走了一遍。搜索 `AskNpcCandidate` 得到一条结果，点开源码，弹窗定位到 `NpcVision.h` 第 26 行。关掉弹窗后，搜索框里的词还在。我如果想继续查同一组接口，就省去了重新输入的麻烦。[Redflag 的离线 API 手册](../output/api/index.html) · [对应的函数声明](../Redflag/src/Redflag/Scene/NpcVision.h)

下次想直接把这种检查交给 agent，可以用微软的 **`playwright-cli` skill**。它教 agent 用命令行打开网页、输入文字、点击按钮和读回浏览器状态。按作者的说明，在装有 Node.js 20 或更新版本的开发机上安装 `@playwright/cli`，然后在项目根目录运行 `playwright-cli install --skills=agents`。给 agent 页面地址和刚才那串操作，让它交回弹窗里高亮的声明、关闭后的搜索词。换成注册页也一样，操作的终点可以是“邮箱填错时，错误文字出现在对应输入框旁边”。[微软的 CLI 安装说明](https://github.com/microsoft/playwright-cli#installation) · [skill 的安装与用法](https://github.com/microsoft/playwright.dev/blob/main/agent-cli/skills.mdx)

还有些问题，拿鼠标顺利点过一遍也看不出来。比如只用键盘按 Tab 在一排按钮间移动，能不能看出下一次按回车会触发哪个按钮？这时可以用 Vercel 的 **`web-design-guidelines` skill** 审查网页源码。我的用法是把范围收在一个文件上，例如“检查 `docs/api/reference.html` 里搜索框、源码按钮和关闭按钮的键盘操作，只列出有问题的代码位置和用户会碰到的现象”。它若指出某个样式隐藏了焦点轮廓，我会再让 Playwright 用 Tab 走到那个控件。这样就知道这条意见在页面上长什么样了。[Vercel 的 skill 与审查规则](https://github.com/vercel-labs/agent-skills/tree/main/skills/web-design-guidelines)

## 一行看着正确的代码，放回旧项目就报错

假设我在维护一个 Python 小工具，用 Pydantic 读取角色配置，现在想把名字和等级导出成字典。这个需求小到只要写一行，却很容易碰到版本问题。同一个操作，Pydantic 1.x 用 `dict()`，到了 2.x，推荐写法换成了 `model_dump()`。[Pydantic 官方迁移对照](https://docs.pydantic.dev/latest/migration/#changes-to-pydanticbasemodel)

> **我**　帮我把角色名字和等级导出成字典，现有项目还要照常运行。
>
> **AI**　可以调用 `player.model_dump()`。
>
> **我**　我用启动这个项目的 Python 跑了这句，报的是“没有 `model_dump` 属性”。先用同一个 Python 查 Pydantic 的版本，再用 Context7 找这一版的写法。拿名字“小林”、等级 3 跑一遍，把返回的字典给我。依赖版本先保留。
>
> **AI**　这个服务的环境用的是 1.10。我用 `player.dict()` 跑了你给的样例，返回 `{'name': '小林', 'level': 3}`。导出函数可以用这一行。

**Context7** 是 Upstash 提供的文档检索插件。插件是可以安装的一包功能，Context7 里面既有指导检索的 skill，也有连接文档库的 MCP 工具。MCP 是 agent 向外部服务请求资料或操作时使用的协议。按作者的说明给 Codex 装上插件，完成账号授权，再开一个新会话，就可以像上面的对话一样给它库名、版本和问题。如果索引里找不到那个旧版本，我会要它给出缺的是哪一版，再回到作者保留的版本文档。这比读完一大段示例才发现版本不合适要省力。[Context7 的 Codex 安装与用法](https://github.com/upstash/context7/blob/master/plugins/codex/context7/README.md)

## 本地通过了，GitHub 上还是红的

把修改提交给队友审查时，GitHub 上的拉取请求叫 PR。许多项目会在 PR 上自动编译、运行测试，这些检查通常合称 CI。如果本机测试通过，PR 上却亮了红叉，我会把**那一次失败检查的链接**发给 agent。在能读取相应仓库的 GitHub MCP 服务中，agent 可以找到失败的任务，展开它的 GitHub Actions 日志。若本机已经登录 GitHub CLI，也可以先用 `gh pr checks 42` 查看第 42 号 PR，再用 `gh run view <运行编号> --log-failed` 读失败步骤。从失败检查的链接里能找到 `/actions/runs/`，它后面的数字就是第二条命令要填的运行编号。[GitHub 官方 MCP 服务](https://github.com/github/github-mcp-server) · [查看 PR 检查](https://cli.github.com/manual/gh_pr_checks) · [读取运行日志](https://cli.github.com/manual/gh_run_view)

假设一个带网页图标的项目在 Windows 上能跑，提交后 Linux 构建失败。下面是我会怎样把一个看起来合理的猜测引回到报错现场。

> **AI**　可能是 Linux 构建机缺少图片依赖，我可以在工作流里加安装步骤。
>
> **我**　先看看失败任务里，具体是哪个命令在读哪一个文件。把原始报错和仓库里登记的文件名放在一起，目录和大小写都保留。
>
> **AI**　日志里要读 `icons/Save.png`，Git 里登记的是 `icons/save.png`。
>
> **我**　那就改引用处的拼写，跑同一个 Linux 构建。等这个检查通过，再把修正和运行链接发给我。

我让它保留大小写，是因为 *Save* 和 *save* 在常见的 Windows 文件系统设置下可能指向同一个文件，而 Linux 构建机通常会区分它们。假如日志指向别的原因，这两行原文也足以让我们及时放弃这个猜测。

## 只有用户的机器上发生了错误

发布之后，有时只能收到一句“我点导出，程序就退了”。上面的 CI 日志记录的是构建过程，想知道用户点按钮之后程序走到了哪里，需要那一次运行留下的信息。如果应用已经把错误上报给 Sentry，可以用 **Sentry MCP**，把问题链接交给有权读取该项目的 agent。我会请它在用户用的那个版本里找一次导出失败的记录，读出异常类型，再沿着出错时的函数调用记录（调用栈）找出最靠近报错的那行项目代码。如果这次还记录了用户点过的按钮，就把出错前的操作也一起列出来。[Sentry 官方 MCP 服务与接入方式](https://github.com/getsentry/toolkit#sentry-mcp) · [Sentry 事件详情里有哪些信息](https://docs.sentry.io/product/issues/issue-details/)

假如调用栈指向保存文件的函数，我们就知道该从哪里开始读源码了。至于用户选了哪种文件、输入里有没有空名字，还得看事件中实际保存了什么。如果那次没有记录文件类型，我会把它列成待确认的问题，再用手边的样例尝试复现。这样别人接手时，能分清哪一步已经查到，哪一步还得继续找。

## 哪些做法值得留下

我会把一项引擎修改记成独立的开发任务。例如改网格创建接口，先让 Agent 查“顶点和索引创建网格”。搜到的同名函数有好几个，点进 `RenderDevice::CreateMesh`，可以看到模型从哪里交出顶点、OpenGL 和 Vulkan 各自在哪里接手。给这次查询附上任务编号，之后无论谁接手，都能查看当时的源码。如果我发现它只跑了 OpenGL，就在这条任务下写“请用同一份网格再跑一次 Vulkan”，下一个人能对着同一版补丁和测试日志复查。[开发资料与操作示例](api/development.md)

当 Agent 说“我想让整个维修站都用 4K 贴图”，我会给一个更容易验证的要求：“先站在玩家进棚时会停的位置。烘一份工具架底色和法线，在同一盏灯下对比锈斑和漆面的高光，给我看原尺寸局部。远处同款架子先沿用原图。” 现在可以直接把这段工作交给项目的材质烘焙 skill，源图、加工方法和替换的位置都有记录。[材质烘焙方法](../.agents/skills/redflag-material-bake/SKILL.md)

让 agent 改关卡时，我会给它一张游戏截图，说清楚站在哪里、希望玩家首先看见什么。比如：“站在西侧入口，我希望一眼看见工棚里的发电机。工具车可以靠近镜头，留出中间那条路。” 这时用 `level-design` 先画出能走的路线，再用 `game-visuals` 看近处的器材是否挡住了门。两份 skill 都可以直接阅读，挑与当前关卡有关的部分用。[关卡设计](https://github.com/mattstruble/skills/tree/ec8c029b9a72f5ed36a683ffcedd046897418116/level-design) · [游戏美术](https://github.com/mattstruble/skills/tree/ec8c029b9a72f5ed36a683ffcedd046897418116/game-visuals)

有时 agent 会拿一张漂亮的概念图回来。我会接着说：“保留这张图作参考。现在请从正式游戏的出生点截一张，把能看到的东西告诉我。转过一个角度，门还能认出来吗？” 这样即使模型还没找到，也能先判断场景布局要不要改。[维修站的场景设计与实机图](../output/pdf/Redflag-Station-Design.pdf)

我也可以给自己的引擎做工具。例如有人说“我选了天空，场景还是黑的”，先用 Redflag MCP 查这个场景保存的环境图、天空开关和显示模式，再让独立的编辑器打开它。如果对方刚刚改了设置，还没按保存，工具读到的和对方眼前的可能是两个版本。这时我会先对上场景与机位，随后再截帧。仓库里把这两种工作分别写成了 `$redflag-scene` 和 `$redflag-frame-check`，以后换场景也能用。[引擎工具的接入与用法](../scripts/redflag_mcp/README.md)

一次查错时，我会把链接、报错、希望看到的结果直接写在对话里。等到同一个过程反复出现，例如每次改手册都得重新编译、打开成品 PDF、找到改过的图、看箭头有没有压住字，我才会把这套做法写进 **`SKILL.md`**。试用时可以故意把一个图框缩窄，看它能否指出哪行字被遮住了。Redflag 里也有现成的样本可试别的工具。比如用 RenderDoc MCP 打开 2D 精灵的截帧，就能拿它读出的结果和已知的两组绘制次数核对，一组 64 次，一组 1 次。[Codex 的 skill 编写指南](https://learn.chatgpt.com/docs/build-skills) · [手册的编译入口](latex/README.md) · [精灵截帧与绘制记录](../output/profiling/batch2d/README.md)

项目规则 **`AGENTS.md`** 留给换一个任务也适用的约定。例如我希望新接口沿用早期 2D 代码的命名，就把一个能打开的头文件给 agent 看。至于“这次先保留旧版 Pydantic”，留在那次任务里就够了。过几个月真要升级依赖，我也不希望半年前随口交代的一句话，又把这次升级拦下来。[Redflag 的项目规则](../AGENTS.md) · [早期的 Renderer2D 接口](../Redflag/src/Redflag/Renderer/Renderer2D.h) · [Codex 如何读取项目规则](https://learn.chatgpt.com/docs/agent-configuration/agents-md)

<details>
<summary>我会怎样写一份检查 PDF 插图的 skill</summary>

假设把下面这段存成 `.agents/skills/pdf-figure-review/SKILL.md`，下次就可以说“用 `$pdf-figure-review` 看一下 RHI 文档里的资源回收图”。

```markdown
---
name: pdf-figure-review
description: 修改 LaTeX 插图后，检查成品 PDF 的文字、箭头和图注。
---
用这本手册的命令编译 PDF，按图题找到所在页，导出并打开整页图片。
如果箭头穿过文字或文字超出边框，指出是哪根箭头、哪个框、哪行字。
交回页码和整页图，方便我按原尺寸阅读。
```

</details>
