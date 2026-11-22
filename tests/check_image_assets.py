"""用两个空项目、CLI、真实 MCP 握手和本机替身端点验证美术素材登记。"""
import asyncio
import base64
from concurrent.futures import ThreadPoolExecutor
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from io import BytesIO
import json
import os
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory
from threading import Thread
from unittest.mock import patch

from mcp import ClientSession, StdioServerParameters, stdio_client
from PIL import Image
from ruamel.yaml import YAML

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts/redflag_mcp"))
sys.path.insert(0, str(ROOT / "scripts"))
import image_assets
from asset_bundle import AssetBundle


def png(color):
    output = BytesIO()
    Image.new("RGBA",(3,2),color).save(output,format="PNG")
    return output.getvalue()


class ImagesFixture(BaseHTTPRequestHandler):
    def do_POST(self):
        request = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        assert self.path == "/v1/images/generations" and self.headers.get("Authorization") == "Bearer sample-key"
        assert request["model"] == "test-image" and request["n"] == 1
        if request["prompt"] == "service unavailable":
            self.send_error(503)
            return
        raw = b"broken" if request["prompt"] == "invalid image" else png("blue")
        payload = json.dumps({"data":[{"b64_json":base64.b64encode(raw).decode(),"revised_prompt":"blue square"}]}).encode()
        self.send_response(200)
        self.send_header("Content-Type","application/json")
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, *_):
        pass


async def check_mcp(project, original, env):
    params = StdioServerParameters(command=sys.executable, args=[str(ROOT/"scripts/redflag_mcp/server.py")],
                                  env={**env,"REDFLAG_PROJECT":str(project)})
    async with stdio_client(params) as streams, ClientSession(*streams, read_timeout_seconds=45) as client:
        await client.initialize()
        async def call(tool, **args):
            result = await client.call_tool(tool,args)
            assert not result.is_error, result.content
            return result.structured_content
        cap = await call("get_capabilities")
        assert Path(cap["project"]) == project
        imported = await call("import_ui_image",collection="menus",name="Token.png",file=str(original))
        args = dict(collection="menus",name="Token.png")
        generated = await call("generate_ui_image",**args,prompt="blue")
        assert generated["id"] == imported["id"]
        layout = "assets/ui/menus/hud.yaml"
        (project/layout).write_text("# 保留设计者的字号\nSprites: {badge: Old.png}\nElements: [{Text: mode, Font: 21}]\n",encoding="utf8")
        assert layout in (await call("list_ui_layouts"))["layouts"]
        saved = await call("inspect_ui_layout",layout=layout)
        assert saved["Sprites"]["badge"] == "Old.png"
        await call("set_ui_sprite",layout=layout,key="badge",previous="Old.png",image=imported["asset"])
        assert (await call("inspect_ui_layout",layout=layout))["Elements"] == saved["Elements"]
        assert "# 保留设计者的字号" in (project/layout).read_text(encoding="utf8")
        stale = await client.call_tool("set_ui_sprite",dict(layout=layout,key="badge",previous="Old.png",image=imported["asset"]))
        assert stale.is_error and (await call("inspect_ui_layout",layout=layout))["Sprites"]["badge"] == "Token.png"
        files = [project/generated[key] for key in ("asset","source")] + [Path(str(project/generated["asset"])+".rfmeta")]
        previous = [file.read_bytes() for file in files]
        assert previous[0] == png("blue")
        record = YAML(typ="safe").load(previous[1])
        assert record["model"] == "test-image" and record["prompt"] == "blue" and record["revised_prompt"]=="blue square"
        assert b"sample-key" not in previous[1]
        for prompt in ("service unavailable","invalid image"):
            failure = await client.call_tool("generate_ui_image",{**args,"prompt":prompt})
            assert failure.is_error and [file.read_bytes() for file in files] == previous
        escape = await client.call_tool("import_ui_image",{**args,"file":str(original),"collection":"../outside"})
        assert escape.is_error and not (project/"assets/outside").exists()

        # 模拟查看器锁住原图，在最后替换 PNG 的一步失败。刚写入的来源记录也应恢复。
        replace = os.replace
        def blocked(source, destination):
            if Path(destination)==files[0]:
                raise PermissionError("image is open")
            return replace(source,destination)
        try:
            with patch.object(image_assets.os,"replace",blocked):
                image_assets.import_png(project,"menus","Token.png",original)
        except PermissionError:
            pass
        else:
            raise AssertionError("failed write was not reported")
        assert [file.read_bytes() for file in files] == previous
        return generated["id"]


def main():
    with TemporaryDirectory() as directory, ThreadingHTTPServer(("127.0.0.1",0),ImagesFixture) as provider:
        thread = Thread(target=provider.serve_forever,daemon=True)
        thread.start()
        temp = Path(directory)
        first, second = temp/"Game A", temp/"Game B"
        for project in (first,second):
            (project/"assets").mkdir(parents=True)
        original = temp/"art.png"
        original.write_bytes(png("orange"))
        env = {k:v for k,v in os.environ.items() if not k.startswith("REDFLAG_IMAGE_")}
        result = subprocess.check_output([sys.executable,str(ROOT/"scripts/redflag_mcp/image_assets.py"),
            "--project",str(first),"import","inventory","Token.png",str(original),"--creator","local artist"],env=env)
        offline = json.loads(result)
        blue = temp/"blue.png"
        blue.write_bytes(png("blue"))
        with ThreadPoolExecutor(max_workers=2) as threads:
            jobs = [threads.submit(image_assets.import_png,first,"concurrent","Shared.png",file) for file in (original,blue)]
            records = [job.result() for job in jobs]
        assert records[0]["id"] == records[1]["id"]
        source = YAML(typ="safe").load((first/records[0]["source"]).read_bytes())
        assert source["asset_sha256"] == image_assets.sha256((first/records[0]["asset"]).read_bytes())
        env.update(REDFLAG_IMAGE_ENDPOINT=f"http://127.0.0.1:{provider.server_port}/v1/images/generations",
                   REDFLAG_IMAGE_MODEL="test-image",REDFLAG_IMAGE_KEY="sample-key")
        try:
            other = asyncio.run(check_mcp(second, original, env))
        finally:
            provider.shutdown()
        assert offline["id"]!=other and (first/offline["asset"]).read_bytes()==png("orange")
        assert not (first/"assets/ui/menus").exists() and not (second/"assets/ui/inventory").exists()
        bundle = AssetBundle(first)
        bundle.add(first/offline["asset"], "HUD")
        manifest = bundle.write(temp/"game.zip", "HUD")
        paths = {row["path"] for row in manifest["files"]}
        assert {offline["asset"], offline["asset"]+".rfmeta",offline["source"]} <= paths
        print("PASS two project roots; offline PNG; MCP import/generate; stable texture ID; prompt/credit; independent UI collections")
        print("PASS HTTP 503, invalid PNG, refused file replacement and path traversal preserve prior assets; provider was a local fixture")


if __name__=="__main__":
    main()
