"""首次运行 --setup 安装依赖。客户端平时启动时直接使用本仓库的 Python 环境。"""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import venv


HERE = Path(__file__).resolve().parent
ENV = HERE.parents[1] / "bin-int/redflag-mcp"
PYTHON = ENV / ("Scripts/python.exe" if os.name == "nt" else "bin/python")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--setup", action="store_true", help="安装本仓库需要的包并检查 MCP 连接")
    if parser.parse_args().setup:
        if sys.version_info < (3, 11):
            parser.exit(1, "请使用 Python 3.11 或更新版本。\n")
        if not PYTHON.is_file():
            venv.EnvBuilder(with_pip=True).create(ENV)
        subprocess.run([PYTHON, "-m", "pip", "install", "-r", HERE / "requirements.txt"], check=True)
        return subprocess.call([PYTHON, HERE / "check_connection.py"])

    if not PYTHON.is_file():
        parser.exit(1, f'请先运行 python "{HERE / "launch.py"}" --setup\n')
    # 服务的标准输入和输出留给 MCP 客户端。安装日志只在 --setup 时打印。
    return subprocess.call([PYTHON, "-X", "utf8", HERE / "server.py"])


if __name__ == "__main__":
    sys.exit(main())
