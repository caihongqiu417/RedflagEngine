"""Run from the repository root with qrenderdoc --python tests/replay_mesh_lod.py."""
import csv
from hashlib import sha256
import json
from pathlib import Path
from statistics import median
import sys

import renderdoc as rd

OUT = Path("output/profiling/mesh-lod")


def draws(actions):
    for action in actions:
        if action.flags & rd.ActionFlags.Drawcall:
            yield action
        yield from draws(action.children)


def replay(api, mode):
    name = f"{api}-{mode}"
    path = Path("output/captures/local") / f"LOD-{name}_capture.rdc"
    capture = rd.OpenCaptureFile()
    controller = None
    try:
        assert capture.OpenFile(str(path), "", None) == rd.ResultCode.Succeeded
        result, controller = capture.OpenCapture(rd.ReplayOptions(), None)
        assert result == rd.ResultCode.Succeeded
        events = list(draws(controller.GetRootActions()))
        count = 2844 if mode == "auto" else 48903
        if api == "OpenGLScene":
            passes = []
            for event in events:
                if event.numIndices != count: continue
                controller.SetFrameEvent(event.eventId, True)
                pipeline = controller.GetPipelineState()
                buffers = [str(target.resource) for target in pipeline.GetOutputTargets()]
                passes.append(dict(event=event.eventId, indices=event.numIndices, colors=buffers,
                                   depth=str(pipeline.GetDepthTarget().resource)))
            assert len(passes) >= 3
            return dict(api=api, mode=mode, file=path.as_posix(), sha256=sha256(path.read_bytes()).hexdigest(),
                        bunny_passes=passes, validation=[m.description for m in controller.GetFrameInfo().debugMessages])
        assert sorted(e.numIndices for e in events) == [36, count]
        bunny = next(e for e in events if e.numIndices == count)
        elapsed = []
        for _ in range(9):
            for sample in controller.FetchCounters([rd.GPUCounter.EventGPUDuration]):
                if sample.eventId == bunny.eventId:
                    elapsed.append(sample.value.d * 1000)
        controller.SetFrameEvent(events[-1].eventId, True)
        image = rd.TextureSave()
        image.resourceId = controller.GetPipelineState().GetOutputTargets()[0].resource
        image.destType = rd.FileType.PNG
        result = controller.SaveTexture(image, str(OUT / (name + ".png")))
        assert result == rd.ResultCode.Succeeded
        samples = list(csv.DictReader((OUT / (name + ".csv")).open()))[-32:]
        return dict(api=api, mode=mode, file=path.as_posix(), sha256=sha256(path.read_bytes()).hexdigest(),
                    draw_calls=len(events), entity_indices=count, floor_indices=36, entity_event=bunny.eventId,
                    gpu_ms=elapsed, gpu_median_ms=median(elapsed),
                    cpu_record_median_us=median(float(s["record_us"]) for s in samples),
                    validation=[m.description for m in controller.GetFrameInfo().debugMessages])
    finally:
        if controller:
            controller.Shutdown()
        capture.Shutdown()


if __name__ == "__main__":
    import traceback
    try:
        report = dict(renderdoc=rd.GetVersionString(), samples=[replay(api, mode)
                      for api in ["OpenGL", "Vulkan", "OpenGLScene"] for mode in ["auto", "full"]])
        (OUT / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    except Exception:
        (Path("tmp") / "mesh-lod-replay-error.txt").write_text(traceback.format_exc())
        sys.exit(1)
    sys.exit(0)
