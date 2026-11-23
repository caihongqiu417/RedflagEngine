"""把引擎使用的每靴六点，对照正式 GLB 按关节权重变形后的整只靴底。"""
import hashlib
import json

import numpy as np

from check_station_soles import ROOT, Rig, read_glb, shoe_mesh, transforms


def measure():
    folder = ROOT/"output/validation/station-actions"
    config = json.loads((folder/"sole-contacts.json").read_text(encoding="utf8"))
    path = ROOT/config["source"]
    assert hashlib.sha256(path.read_bytes()).hexdigest() == config["model_sha256"]
    rig = Rig(*read_glb(path))
    rest = transforms(rig.world(rig.pose({"channels":[]},[0])))
    source = next(node for node in rig.nodes if node["name"] == config["mesh"])
    skin = rig.doc["skins"][source["skin"]]
    bones = np.array(skin["joints"])
    inverse = rig.values(skin["inverseBindMatrices"]).reshape(-1,4,4).transpose(0,2,1)
    primitive = rig.doc["meshes"][source["mesh"]]["primitives"][config["primitive"]]
    attrs = primitive["attributes"]

    shoes = {}
    for side in ("L","R"):
        points = config[side+"6"]["points"]
        node = np.array([rig.names[point["bone"]] for point in points])
        local = np.array([point["xyz"]+[1.] for point in points])
        vertex = np.array([point["vertex"] for point in points])
        ids = rig.values(attrs["JOINTS_0"])[vertex]
        weights = rig.values(attrs["WEIGHTS_0"])[vertex]
        raw = np.column_stack((rig.values(attrs["POSITION"])[vertex], np.ones(len(vertex))))
        bound = np.einsum("vkij,vj->vki", inverse[ids], raw)
        original = np.einsum("vkrj,vkj,vk->vr",rest[0,bones[ids]],bound,weights)
        from_bone = np.einsum("vij,vj->vi",rest[0,node],local)
        # 保存在角色配置中的六位小数，应能在绑定姿势中找回同一批真实的模型顶点。
        assert np.linalg.norm(original-from_bone,axis=1).max() < .000002
        pieces, _ = shoe_mesh(rig,rest,side)
        shoes[side] = node,local,pieces

    def height(world, side):
        node,local,pieces = shoes[side]
        y = world[:,:,1,:]
        approx = np.einsum("tvj,vj->tv",y[:,node],local).min(axis=1)
        full = np.min([np.einsum("fvkj,vkj->fv",y[:,indices],bound).min(axis=1)
                       for indices,bound in pieces],axis=0)
        return full, approx

    reference = {side:tuple(float(v[0]) for v in height(rest,side)) for side in shoes}
    result = {"model_sha256":config["model_sha256"],"rate_hz":120,"clips":{}, "tiptoe":{}}
    for clip in rig.doc["animations"]:
        name = clip["name"]
        duration = max(float(rig.values(s["input"])[-1,0]) for s in clip["samplers"])
        times = np.linspace(0,duration,round(120*duration)+1)
        local = rig.pose(clip,times)
        root = rig.names["root"]
        local[root]["translation"] = np.tile(rig.nodes[root].get("translation",[0,0,0]),(len(times),1))
        world = transforms(rig.world(local))
        result["clips"][name] = {}
        for side in shoes:
            full, approx = height(world,side)
            delta = (approx-reference[side][1]) - (full-reference[side][0])
            result["clips"][name][side] = dict(samples=len(times), max_height_error_mm=float(np.max(abs(approx-full))*1000),
                max_lift_error_mm=float(np.max(abs(delta))*1000), p95_lift_error_mm=float(np.quantile(abs(delta),.95)*1000))
            if name == "walking_gun-cycle":
                frame = 122 if side == "L" else 39  # 左脚和右脚分别处于抬跟、前掌仍贴地的时刻。
                result["tiptoe"][side] = dict(seconds=float(times[frame]),full_sole_y_m=float(full[frame]),
                    six_points_y_m=float(approx[frame]),full_lift_m=float(full[frame]-reference[side][0]),
                    six_points_lift_m=float(approx[frame]-reference[side][1]))
    (folder/"sole-contact-check.json").write_text(json.dumps(result,indent=2)+"\n",encoding="utf8")
    return result


if __name__ == "__main__":
    result = measure()
    for side in ("L","R"):
        worst = max((row[side]["max_lift_error_mm"],clip) for clip,row in result["clips"].items())
        print(side, "maximum sole lift error (mm), clip:", worst, "tiptoe:", result["tiptoe"][side])
        if worst[0] > 10:
            raise AssertionError(f"{side} boot: the six-point estimate misses the full sole by more than 1 cm")
