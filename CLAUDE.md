# 在 Redflag 中工作

处理引擎或游戏任务时，先调用项目 MCP `redflag` 的 `get_capabilities`，从 `get_workflow` 取得当前任务要用的操作。可以自己查资产、编译、进游戏并按实际结果继续。交付时附构建或试玩编号、观察到的事件，以及仍需人判断的地方。文件改动沿用 [项目规范](AGENTS.md)。

首次克隆若缺少服务依赖，可在仓库根目录运行 `python scripts/redflag_mcp/launch.py --setup`。Claude Code 首次加载这个项目需要开发者允许 `redflag`。可用 `/mcp` 查看状态。[连接说明](scripts/redflag_mcp/README.md)
