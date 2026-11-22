"""从项目根目录运行 python tests/profile_ssaa.py <冻结的编辑器.exe> capture|replay|timing|gpu。"""
import argparse
import gzip
import json
import os
from pathlib import Path
import re
from statistics import median

import yaml

from capture_material_studio import run_renderdoc
from profile_fine_mask import digest
from profile_native_navigation import prepare
from profile_reflection_editor import frame_samples, metrics

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "output/profiling/ssaa"
TOOLS = ROOT / "tmp/tools/renderdoc/RenderDoc_1.47_64"
BIN = ROOT / "bin/Debug-windows-x86_64/RedflagForge"


def setup(exe, mode):
    tag = digest(exe)[:12]
    work = ROOT / "tmp/ssaa-profile" / tag / mode
    prepare(work)
    scene = work / "assets/scenes/Sponza-Atrium.redflag"
    settings = dict(Shading="Lit", Direct="Clustered lights", Indirect="DDGI", GTAO=True, TAA=True, SSR=True,
                    VolumetricFog=True, FogDensity=.06, FogSteps=8, SceneReflections=True,
                    Samples=2, HistoryWeight=.85, PreviewWidth=384, Supersampling=(mode == "on"))
    Path(str(scene)+".render.yaml").write_text(yaml.safe_dump(settings), encoding="utf8")
    return work, scene, settings


def capture(exe, mode, tools):
    work, scene, settings = setup(exe, mode)
    name = "Sponza-SSAA-" + digest(exe)[:12] + "-" + mode
    store = ROOT / "output/captures/local" / name
    store.mkdir(parents=True, exist_ok=True)
    (work / "imgui-vulkan.ini").unlink(missing_ok=True)
    (work / "frame-path.txt").unlink(missing_ok=True)
    env = dict(os.environ, PATH=str(BIN)+os.pathsep+os.environ["PATH"], RF_VULKAN_VALIDATION="1",
               RF_STUDIO_EXE=str(exe), RF_STUDIO_WORK=str(work), RF_STUDIO_TOOLS=str(tools),
               RF_STUDIO_CAPTURE=str(store / "studio_frame24.rdc"))
    run_renderdoc(tools, ROOT / "tests/capture_material_studio.py", ROOT, env)
    frame = Path((work/"frame-path.txt").read_text(encoding="utf8")).resolve(strict=True)
    gpu = re.search(rb"Vulkan device: ([^\r\n]+)", (work/"editor.log").read_bytes())[1].decode()
    data = dict(executable_sha256=digest(exe), device=gpu, validation_layer=True, frame=frame.relative_to(ROOT).as_posix(), frame_sha256=digest(frame),
                scene_sha256=digest(scene), settings=settings, settings_sha256=digest(Path(str(scene)+".render.yaml")))
    (OUT/(mode+"-capture.json")).write_text(json.dumps(data,indent=2)+"\n",encoding="utf8")
    print(mode, frame, flush=True)


def timing(exe):
    import win32con, win32service
    from profile_fine_mask_editor import measure
    name=f"SSAATiming{os.getpid()}"
    desktop=win32service.CreateDesktop(name,0,win32con.GENERIC_ALL,None)
    env=dict(os.environ, PATH=str(BIN)+os.pathsep+os.environ["PATH"])
    for key in ("RF_VULKAN_VALIDATION","VK_INSTANCE_LAYERS","VK_ADD_LAYER_PATH","ENABLE_VULKAN_RENDERDOC_CAPTURE"):
        env.pop(key,None)
    traces=OUT/"traces"
    traces.mkdir(exist_ok=True)
    runs=[]
    try:
        for i,mode in enumerate(("off","on","on","off","off","on")):
            work,scene,settings=setup(exe,mode)
            (work/"imgui-vulkan.ini").unlink(missing_ok=True)
            log=work/("timing-"+str(i)+".log")
            cpu=measure(exe,name,work,env,log)
            raw=(work/"RedflagProfile-Runtime.json").read_bytes()
            sampled,cached=frame_samples(json.loads(raw)["traceEvents"])
            dest=traces/(str(i+1)+"-"+mode+".json.gz")
            dest.write_bytes(gzip.compress(raw,mtime=0))
            text=log.read_bytes()
            shape=[int(v) for v in re.findall(rb"Lighting preview (\d+) x (\d+), viewport (\d+) x (\d+)",text)[-1]]
            row=dict(mode=mode,run=i+1,shape=shape,cpu_process_ms=cpu,trace=dest.relative_to(OUT).as_posix(),
                     frame_ms=[r[1] for r in sampled],lighting_ms=[r[2] for r in sampled],cached_ms=[r[1] for r in cached])
            runs.append(row)
            print(i+1,mode,shape,"redraw P50",median(row["frame_ms"]),flush=True)
    finally:
        desktop.CloseDesktop()
    summary={mode:{field:metrics([t for row in runs if row["mode"]==mode for t in row[field]])
                   for field in ("frame_ms","lighting_ms","cached_ms")} for mode in ("off","on")}
    report=dict(executable_sha256=digest(exe),runs=runs,summary=summary,renderdoc=False,validation=False,
                scope="main thread RunLoop including GPU waits and swapchain presentation. Three rotated launches per mode; per launch skip first eight fixed-camera draws, use 24 redraws and 16 cached frames.")
    (OUT/"editor.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf8")


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe",type=Path)
    parser.add_argument("step",choices=("capture","replay","timing","gpu"))
    parser.add_argument("--renderdoc",type=Path,default=TOOLS)
    args=parser.parse_args()
    exe=args.exe.resolve(strict=True)
    OUT.mkdir(parents=True,exist_ok=True)
    if args.step=="timing":
        timing(exe)
        return
    if args.step=="gpu":
        run_renderdoc(args.renderdoc.resolve(),ROOT/"tests/replay_ssaa.py",ROOT,
                      dict(os.environ,RF_SSAA_TIMINGS=str(OUT),RF_SSAA_EXE_SHA=digest(exe)))
        values=json.loads((OUT/"gpu.json").read_bytes())
        if "error" in values: raise RuntimeError(values["error"])
        return
    for mode in ("off","on"):
        if args.step=="capture": capture(exe,mode,args.renderdoc.resolve())
        else:
            work,_,_=setup(exe,mode)
            metadata=json.loads((OUT/(mode+"-capture.json")).read_bytes())
            assert digest(exe)==metadata["executable_sha256"]
            run_renderdoc(args.renderdoc.resolve(),ROOT/"tests/replay_ssaa.py",ROOT,
                          dict(os.environ,RF_SSAA_CAPTURE=str(ROOT/metadata["frame"]),RF_SSAA_WORK=str(work)))
            result=json.loads((work/"ssaa-replay.json").read_bytes())
            if "error" in result: raise RuntimeError(result["error"])
            run_renderdoc(args.renderdoc.resolve(),ROOT/"tests/replay_material_studio.py",ROOT,
                          dict(os.environ,RF_STUDIO_CAPTURE=str(ROOT/metadata["frame"]),RF_STUDIO_EXPORT=str(work)))
            studio=json.loads((work/"replay.json").read_bytes())
            p=result["panel"]
            assert studio["shading_size"]==p["source_size"] and studio["size"]==p["size"]
            assert studio["pack_event"]==result["pack"]["event"] and studio["display_event"]==p["event"]
            samples=p["source_size"][0]*p["source_size"][1]
            assert (work/"surface.f32").stat().st_size==(work/"light.f32").stat().st_size==samples*16
            assert digest(work/"light.f32")==digest(work/"ssaa-light.f32")
            assert digest(work/"display.f32")==digest(work/"ssaa-panel.f32")
            result["shared_studio_reader"]=dict(shading_size=studio["shading_size"],samples_per_pixel=studio["samples_per_pixel"],
                files={name:dict(bytes=(work/name).stat().st_size,sha256=digest(work/name)) for name in ("surface.f32","light.f32","display.f32")})
            from verify_ssaa import verify
            verify(work,OUT,mode,result)
    if args.step=="replay":
        from verify_ssaa import comparison
        comparison(OUT)


if __name__=="__main__":
    main()
