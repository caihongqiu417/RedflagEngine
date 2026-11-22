"""核对 CLI 和 MCP，并在一份隔离的项目中保留失败、修正、成功和过期的复测。"""
import asyncio
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/"scripts"))
import context_index
import context_review


def local(root):
    project = root/"game"
    file = "tests/count.py"
    path = root/file
    path.parent.mkdir()
    path.write_text("assert 1 == 2\n", encoding="utf8")
    task = context_review.begin(root, project, "校正计数", [file, "tests/new.cs"])["task"]
    check = [sys.executable, file]
    first = context_review.verify(root, project, task, check)
    assert first["exit_code"] == 1 and b"AssertionError" in Path(first["log"]).read_bytes()
    path.write_text("assert 2 == 2\n", encoding="utf8")
    (root/"tests/new.cs").touch()
    answer = {"source":f"{file}:1","symbol":"count","lines":[{"line":1,"text":"assert 2 == 2"}]}
    saved = context_review.record_query(root, project, task, "show", f"{file}:1", answer)
    evidence = Path(saved["record"])
    sha = json.loads(evidence.read_bytes())["sources"][file]
    assert (evidence.parents[1]/"sources"/sha).read_bytes() == path.read_bytes()
    second = context_review.verify(root, project, task, check)
    assert second["exit_code"] == 0 and b"+assert 2 == 2" in Path(second["diff"]).read_bytes()
    with ThreadPoolExecutor(2) as pool:
        list(pool.map(lambda msg:context_review.note(project,task,msg), ("保留原例","改用第二组数据")))
    review = context_review.review(root, project, task)
    assert {c["exit_code"]:c["current"] for c in review["checks"]} == {1:False, 0:True}
    assert review["files"]["tests/new.cs"]["changed"] and len(review["notes"]) == 2
    path.write_text("assert 3 == 3\n", encoding="utf8")
    assert not any(c["current"] for c in context_review.review(root,project,task)["checks"])
    assert context_review.tasks(project)["tasks"][0]["task"] == task

    # 用同一份离线索引改动声明，下一次查询必须指明索引已经过期。
    relative = "Redflag/src/Example.h"
    header = root/relative
    header.parent.mkdir(parents=True)
    header.write_text("void Foo();\n",encoding="utf8")
    data = {"entries":[["核心","Redflag","Foo","方法","void Foo()",relative,1,""]],
            "sources":{relative:["void Foo();"]}}
    index = root/"output/api/index.html"
    index.parent.mkdir(parents=True)
    index.write_text('<script id="db" type="application/json">'+json.dumps(data)+'</script>',encoding="utf8")
    assert context_index.show(root,"Foo")["index_current"]
    header.write_text("void Foo(int count);\n",encoding="utf8")
    stale = context_index.show(root,"Foo")
    assert stale["index_current"] is False and "implementation" not in stale
    for invalid in ("../other", "output/api/index.html", ".git/config", str(ROOT/"docs/AI-Workflow.md")):
        try:
            context_review.begin(root,project,"bad",[invalid])
        except ValueError:
            continue
        raise AssertionError(invalid)


async def wire(project):
    from mcp import ClientSession, StdioServerParameters, stdio_client
    server = StdioServerParameters(command=sys.executable, args=[str(ROOT/"scripts/redflag_mcp/server.py")],
                                  env=dict(os.environ, REDFLAG_PROJECT=str(project)))
    async with stdio_client(server) as streams, ClientSession(*streams) as session:
        await session.initialize()

        async def call(name, **arguments):
            result = await session.call_tool(name,arguments)
            assert not result.is_error, result.content
            return result.structured_content

        start = await call("begin_dev_task",intent="核对建网格的接口",files=[])
        task = start["task"]
        answer = await call("lookup_dev_context",query="顶点和索引创建网格",limit=2)
        assert "RenderDevice::CreateMesh" in answer["matches"][0]["symbol"]
        symbol = answer["matches"][0]["symbol"]
        cli = subprocess.check_output([sys.executable, str(ROOT/"scripts/dev_context.py"),"show",symbol,"--limit","2"])
        result = await call("lookup_dev_context",query=symbol,operation="show",limit=2,task=task)
        assert Path(result.pop("record")).is_file() and result == json.loads(cli)
        assert any("Mesh.cpp" in item["source"] for item in result["references"])
        assert len(result["implementation"]) == 2 and result["tests"] and result["runners"]
        file, _, number = result["implementation"][0]["source"].rpartition(":")
        assert context_index.show(ROOT, f"{file}:{number}")["lines"]
        await call("comment_dev_task",task=task,text="请同时检查 Vulkan 后端")
        review = await call("review_dev_task",task=task)
        assert review["notes"] == ["请同时检查 Vulkan 后端"] and review["references"][0]["query"] == symbol
        assert task == (await call("review_dev_task"))["tasks"][0]["task"]
        assert (await session.call_tool("lookup_dev_context",{"query":"x","limit":-2})).is_error
        assert (await session.call_tool("review_dev_task",{"task":"../unrelated"})).is_error


if __name__ == "__main__":
    with TemporaryDirectory() as temp:
        directory = Path(temp)
        local(directory)
        asyncio.run(wire(directory/"mcp-project"))
    print("开发上下文通过。CLI 与 MCP 相同，保留源码、补丁、失败与成功的复测，以及两份并发批注。")
