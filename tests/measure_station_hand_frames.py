"""按动作区间对齐游戏帧、CPU 跟踪和 RenderDoc 回放，保存可独立复算的数据。"""
import argparse
import gzip
import json
from pathlib import Path
import shutil
from statistics import median


def series(rows,start,end):
    return [round(r["dur"]/1000,5) for r in rows if start<=r["ts"]<end]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("raw",type=Path);parser.add_argument("out",type=Path)
    args=parser.parse_args();args.out.mkdir(parents=True,exist_ok=True);runs=[]
    for folder in sorted(args.raw.glob("trace-*/player-heal")):
        name=folder.parent.name
        frames=list(map(json.loads,gzip.open(folder/"frames.jsonl.gz","rt",encoding="utf8")))
        trace=json.load(gzip.open(folder/"cpu-trace.json.gz","rt",encoding="utf8"))["traceEvents"]
        loops=sorted((e for e in trace if e["name"]=="RunLoop"),key=lambda e:e["ts"])
        assert len(loops)==len(frames) and all(f["seq"]==i+1 for i,f in enumerate(frames))
        holds=[f["seq"] for f in frames if next(a for a in f["actors"] if a["id"]=="1101").get("contact")]
        # RunLoop 结束时才能知道本帧用了多久。下一帧 frames.jsonl 的 frame_ms 才报告这段时间。
        start=loops[holds[0]-1]["ts"];end=loops[holds[-1]]["ts"]+loops[holds[-1]]["dur"]
        def values(label):return series([r for r in trace if r["name"]==label],start,end)
        durations=values("RunLoop")
        take=json.loads((folder/"take.json").read_bytes())
        runs.append(dict(run=name,exe_sha256=take["exe_sha256"],zip_sha256=take["archive_sha256"],
                         contact_seq=[holds[0],holds[-1]],frames_ms=durations,median_frame_ms=median(durations),
                         bvh_ms=values("GI / Build BVH"),surface_pass_cpu_ms=values("Rasterize lighting surfaces")))
        for source,suffix in (("cpu-trace.json.gz","-cpu.json.gz"),("frames.jsonl.gz","-play.jsonl.gz")):
            shutil.copy2(folder/source,args.out/(name+suffix))
    captures={}
    for label,folder in (("before","rdc-control"),("after","rdc-new")):
        data=json.loads((args.raw/folder/"replay.json").read_bytes())
        data["capture"]=Path(data["capture"]).relative_to(Path(__file__).resolve().parents[1]).as_posix()
        captures[label]=data
    report=dict(scene="Watt-Station",renderer="Vulkan",internal_pixels=[1280,720],display_pixels=[1920,1080],
                cpu="AMD Ryzen 9 8945HX",gpu="NVIDIA GeForce RTX 5070 Ti Laptop GPU",driver="616.56",
                runs=runs,renderdoc=captures)
    assert all(any(r["bvh_ms"])==r["run"].endswith("before") for r in runs)
    assert any(g["depth"]==[512,512] for g in captures["after"]["model_passes"])
    (args.out/"results.json").write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",encoding="utf8")


if __name__=="__main__":main()
