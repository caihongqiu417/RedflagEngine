"""为本机 AI 客户端生成 Redflag MCP 的启动配置。默认只打印。"""
import argparse
from datetime import datetime
import json
import os
from pathlib import Path
import sys
from tempfile import NamedTemporaryFile


ENGINE = Path(__file__).resolve().parents[2]
CLIENTS = ("generic", "claude-desktop", "claude-code", "cursor", "vscode", "codex")


def server_config(project):
    project = project.expanduser().resolve()
    if not (project / "assets").is_dir():
        raise ValueError(f"游戏根目录应包含 assets 文件夹：{project}")
    python = ENGINE / "bin-int/redflag-mcp" / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    return {"command": str(python), "args": [str(ENGINE / "scripts/redflag_mcp/server.py")],
            "env": {"REDFLAG_PROJECT": str(project)}}


def client_config(client, server):
    if client == "codex":
        def quoted(value):
            return json.dumps(value, ensure_ascii=False)
        return ("[mcp_servers.redflag]\n"
                f"command = {quoted(server['command'])}\n"
                f"args = {quoted(server['args'])}\n"
                "tool_timeout_sec = 180\n\n"
                "[mcp_servers.redflag.env]\n"
                f"REDFLAG_PROJECT = {quoted(server['env']['REDFLAG_PROJECT'])}")
    if client == "vscode":
        config = {"servers": {"redflag": {"type": "stdio", **server}}}
    else:
        config = {"mcpServers": {"redflag": server}}
    return json.dumps(config, ensure_ascii=False, indent=2)


def desktop_path():
    if os.name == "nt":
        return Path(os.environ["APPDATA"]) / "Claude/claude_desktop_config.json"
    if sys.platform == "darwin":
        return Path.home() / "Library/Application Support/Claude/claude_desktop_config.json"
    raise ValueError("请用 --config 指定 Claude Desktop 配置文件。")


def install_desktop(path, server):
    from playtest import writer_lock

    path = path.expanduser().absolute()
    if path.is_symlink():
        raise ValueError(f"配置是符号链接；请用 --config 指定实际文件：{path.resolve()}")
    path.parent.mkdir(parents=True, exist_ok=True)
    # 多个项目可以同时接同一个客户端。两个安装程序先后合并，避免后一份盖掉前一份。
    with writer_lock(path.parent, "." + path.name + ".redflag-lock"):
        return merge_desktop(path, server)


def merge_desktop(path, server):
    original = path.read_bytes() if path.exists() else None
    config = json.loads(original.decode("utf-8-sig")) if original is not None else {}
    if not isinstance(config, dict) or not isinstance(config.get("mcpServers", {}), dict):
        raise ValueError("Claude Desktop 配置及 mcpServers 都应为 JSON 对象。")
    servers = config.setdefault("mcpServers", {})
    if "redflag" in servers:
        if servers["redflag"] == server:
            return f"已配置：{path}"
        raise ValueError(f"已存在不同的 redflag 配置。请比较打印的配置并手动选择：{path}")
    servers["redflag"] = server

    encoding = "utf-8-sig" if original and original.startswith(b"\xef\xbb\xbf") else "utf-8"
    newline = "\r\n" if original and b"\r\n" in original else "\n"
    payload = (json.dumps(config, ensure_ascii=False, indent=2) + "\n").replace("\n", newline).encode(encoding)
    with NamedTemporaryFile(dir=path.parent, delete=False) as temp:
        stage = Path(temp.name)
    try:
        stage.write_bytes(payload)
        if original is None:
            # 如果另一进程刚刚创建配置，保留它的文件并让调用者重试。
            os.link(stage, path)
            return f"已添加 redflag：{path}"

        stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
        with NamedTemporaryFile(dir=path.parent, prefix=f"{path.name}.{stamp}-", suffix=".bak", delete=False) as backup:
            backup.write(original)
        if path.read_bytes() != original:
            raise ValueError("其他程序刚修改了配置，请重新运行。")
        os.replace(stage, path)
        return f"已添加 redflag：{path}\n原配置备份：{backup.name}"
    finally:
        stage.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--client", choices=CLIENTS, help="打印客户端配置，默认 generic")
    mode.add_argument("--install", choices=["claude-desktop"], help="将 redflag 添加到本机的 Claude Desktop 配置")
    parser.add_argument("--project", type=Path, default=ENGINE / "RedflagForge", help="游戏根目录，内含 assets")
    parser.add_argument("--config", type=Path, help="安装目标 JSON，省略时使用 Claude Desktop 的默认位置")
    args = parser.parse_args()
    if args.config and not args.install:
        parser.error("--config 仅和 --install 一起使用。")
    try:
        server = server_config(args.project)
        if args.install:
            if not Path(server["command"]).is_file():
                raise ValueError("请先在仓库里创建 bin-int/redflag-mcp 虚拟环境并安装 requirements.txt。")
            print(install_desktop(args.config or desktop_path(), server))
        else:
            print(client_config(args.client or "generic", server))
    except (OSError, UnicodeError, ValueError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    if not sys.stdout.isatty():
        sys.stdout.reconfigure(encoding="utf-8")
    main()
