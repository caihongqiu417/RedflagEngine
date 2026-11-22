"""Capture Watt-Station in --play using the saved scene and its real third-person camera."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil

import yaml
from capture_material_studio import run_renderdoc
from profile_native_navigation import ROOT, prepare


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("view", choices=("spawn", "gravel", "rack", "rim", "tracks"))
    cli.add_argument("variant", choices=("original", "normal", "current"))
    cli.add_argument("--exe", type=Path, default=ROOT/"bin/Debug-windows-x86_64/RedflagForge/RedflagForge.exe")
    cli.add_argument("--renderdoc", type=Path, default=ROOT/"tmp/tools/renderdoc/RenderDoc_1.47_64")
    cli.add_argument("--study", type=Path, help="Keep a separate capture and report directory for each executable")
    cli.add_argument("--replay", action="store_true")
    cli.add_argument("--time", action="store_true")
    cli.add_argument("--linear", action="store_true", help="Read the game color before exposure and tone mapping")
    args = cli.parse_args()
    exe, rd = args.exe.resolve(strict=True), args.renderdoc.resolve(strict=True)
    label = args.view + "-" + args.variant
    version = hashlib.sha256(exe.read_bytes()).hexdigest()
    collection = "station-tracks" if args.view == "tracks" else "station-materials"
    if args.study:
        if args.study.is_absolute() or ".." in args.study.parts:
            cli.error("--study must name a directory under output/validation")
        collection = args.study
    work = ROOT/"tmp"/collection/version[:12]/label
    out = ROOT/"output/validation"/collection
    if args.study:
        out = out/version[:12]
    out.mkdir(parents=True, exist_ok=True)
    source = ROOT/"RedflagForge/assets/scenes/Watt-Station.redflag"
    rdc = ROOT/"output/captures/local"/collection/version[:12]/label/"game.rdc"
    rdc.parent.mkdir(parents=True, exist_ok=True)
    if not args.replay:
        prepare(work)
        scene = yaml.safe_load(source.read_text(encoding="utf8"))
        entities = {e["Entity"]:e for e in scene["Entities"]}
        ground = entities[1222]
        camera = entities[1100]
        if args.view == "tracks":
            if args.variant == "original": scene["Entities"].remove(entities[1280])
        elif args.view in ("rack", "rim"):
            target = entities[1221 if args.view == "rack" else 1215]
            if args.variant == "original": del target["ModelRendererComponent"]["OverrideMaterials"]
        elif args.variant == "original":
            del ground["ModelRendererComponent"]["OverrideMaterials"]
            ground["TransformComponent"]["Scale"] = [100,100,1]
        elif args.variant == "normal":
            ground["ModelRendererComponent"]["OverrideMaterials"][0]["HeightMode"] = "Off"
        if args.view not in ("spawn", "tracks"):
            yaw = -1.5707963
            if args.view == "rack":
                position, pitch, height, distance = [5.65,.08,2.74], -.18, 1.4, .95
            elif args.view == "rim":
                position, pitch, height, distance = [-5.21,.08,3.4], -.30, .49, .65
                yaw = 3.1415927
            else:
                position, pitch, height, distance = [-6.9,.08,4.5], -.67, 1.0, 1.8
            entities[1101]["TransformComponent"]["Translation"] = position
            camera["TransformComponent"]["Rotation"] = [pitch,yaw,0]
            camera["ScriptComponent"]["ScriptFields"].extend([
                {"Name":"Height", "Type":"Float", "Value":height},
                {"Name":"Distance", "Type":"Float", "Value":distance}])
        entry = work/"assets/scenes/Watt-Station.redflag"
        entry.write_text(yaml.safe_dump(scene,sort_keys=False,allow_unicode=True),encoding="utf8")
        shutil.copyfile(str(source)+".render.yaml",str(entry)+".render.yaml")
        env = dict(os.environ, RF_STUDIO_GAME=str(entry), RF_STUDIO_EXE=str(exe),
                   RF_STUDIO_WORK=str(work), RF_STUDIO_TOOLS=str(rd), RF_STUDIO_CAPTURE=str(rdc),
                   RF_STUDIO_FRAME="12", REDFLAG_PLAYTEST=str(work/"session"),
                   PATH=str(exe.parent)+os.pathsep+os.environ["PATH"])
        (work/"frame-path.txt").unlink(missing_ok=True)
        run_renderdoc(rd, ROOT/"tests/capture_material_studio.py", ROOT, env)
    file = Path((work/"frame-path.txt").read_text(encoding="utf8").strip())
    env = dict(os.environ, RF_RELIEF_RDC=str(file), RF_RELIEF_WORK=str(work), RF_RELIEF_LABEL=label,
               RF_RELIEF_TIME=str(int(args.time)), RF_RELIEF_OUT=str(out), RF_RELIEF_LINEAR=str(int(args.linear)))
    run_renderdoc(rd, ROOT/"tests/replay_station_materials.py", ROOT, env)
    data = json.loads((out/(label+".json")).read_text(encoding="utf8"))
    assert data["size"] == [1920,1080] and data["frame"] == 12
    print(label, data["material_draws"], data["size"])


if __name__ == "__main__":
    main()
