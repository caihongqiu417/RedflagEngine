"""一个工具进程服务一个项目。引擎提供编辑器和 SDK，游戏提供 assets 目录。"""
import os
from pathlib import Path

ENGINE = Path(__file__).resolve().parents[2]
PROJECT = Path(os.environ.get("REDFLAG_PROJECT") or ENGINE / "RedflagForge").resolve()
