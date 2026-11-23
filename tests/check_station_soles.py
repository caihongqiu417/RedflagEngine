"""用同一组三角形比较站姿和步态的靴底方向。运行时需要模型导入器使用的 NumPy、SciPy。"""
import argparse
import hashlib
import json
import sys
from pathlib import Path

import numpy as np
from scipy.spatial.transform import Rotation

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/"scripts"))
from retarget_station_motion import Rig, read_glb


def unit(v):
    return v / np.linalg.norm(v, axis=-1, keepdims=True)


def transforms(world):
    frames = max(len(pos) for pos, _, _ in world.values())
    matrices = np.empty((frames,len(world),3,4))
    for i,(pos,rot,scale) in world.items():
        matrices[:,i,:,:3] = rot.as_matrix() * scale[:,None,:]
        matrices[:,i,:,3] = pos
    return matrices


def basis(triangle):
    a = unit(triangle[:,1]-triangle[:,0])
    n = unit(np.cross(a,triangle[:,2]-triangle[:,0]))
    return np.stack((a,np.cross(n,a),n),axis=-1)


def shoe_mesh(rig, rest, side):
    targets = [rig.names[n+"."+side] for n in ("foot","footcover","toe")]
    candidates, pieces = [], []
    for node in rig.nodes:
        if "skin" not in node: continue
        skin = rig.doc["skins"][node["skin"]]
        bones = np.array(skin["joints"])
        inverse = rig.values(skin["inverseBindMatrices"]).reshape(-1,4,4).transpose(0,2,1)
        for part,primitive in enumerate(rig.doc["meshes"][node["mesh"]]["primitives"]):
            attrs = primitive["attributes"]
            indices = rig.values(attrs["JOINTS_0"])
            weight = rig.values(attrs["WEIGHTS_0"])
            in_shoe = (np.isin(bones[indices],targets)*weight).sum(axis=1) > .8
            if not np.any(in_shoe): continue
            raw = rig.values(primitive["indices"]).ravel().reshape(-1,3)
            select = raw[in_shoe[raw].all(axis=1)]
            if not len(select): continue
            local_index = np.cumsum(in_shoe)-1
            actual = np.flatnonzero(in_shoe)
            indices, weight = indices[in_shoe], weight[in_shoe]
            rest_vertex = np.column_stack((rig.values(attrs["POSITION"])[in_shoe],np.ones(len(actual))))
            bound = np.einsum("vkij,vj->vki",inverse[indices],rest_vertex) * weight[:,:,None]
            piece = (bones[indices],bound)
            points = vertices(rest,piece)[0]
            triangles = points[local_index[select]]
            cross = np.cross(triangles[:,1]-triangles[:,0],triangles[:,2]-triangles[:,0])
            area = np.linalg.norm(cross,axis=1)
            valid = (area > 1.e-6) & (abs(cross[:,1]) > .94*area)
            for j in np.flatnonzero(valid):
                candidates.append((triangles[j].mean(axis=0),area[j],len(pieces),local_index[select[j]],
                                   [node["name"],part,select[j].tolist()]))
            pieces.append(piece)
    # 只在靴底最低处附近选择面片。前后各选一片面积较大的三角形，避开沟槽竖直的小面。
    bottom = min(item[0][1] for item in candidates)
    contact = [item for item in candidates if item[0][1] < bottom+.035]
    return pieces, contact


def vertices(matrices, piece):
    indices, bound = piece
    return np.einsum("fvkrj,vkj->fvr",matrices[:,indices],bound)


def check(path):
    rig = Rig(*read_glb(path))
    idle = rig.world(rig.pose(rig.clip("Idle-cycle"),[0]))
    rest = transforms(idle)
    sides = {}
    for side in ("L","R"):
        parts, candidates = shoe_mesh(rig, rest, side)
        forward = unit(idle[rig.names["toe."+side]][0][0] - idle[rig.names["foot."+side]][0][0])
        depth = np.array([np.dot(item[0],forward) for item in candidates])
        markers = {}
        for key, mask in (("heel",depth<=np.quantile(depth,.33)),("forefoot",depth>=np.quantile(depth,.67))):
            center,area,part,idx,label = max((item for item,keep in zip(candidates,mask) if keep),key=lambda item:item[1])
            old = basis(vertices(rest,parts[part])[:,idx])[0]
            up = old[:,2] * np.sign(old[1,2])
            axis = unit(forward-up*np.dot(up,forward))
            markers[key] = (part,idx,old,up,axis,label)
        sides[side] = parts,markers
    output = {"model_sha256":hashlib.sha256(path.read_bytes()).hexdigest(),"clips":{}}
    for name in ("walking_gun-cycle","walking_nogun-cycle","running_gun-cycle","running_nogun-cycle","Station-CrouchWalk"):
        if name not in [a["name"] for a in rig.doc["animations"]]: continue
        clip = rig.clip(name)
        duration = max(float(rig.values(s["input"])[-1,0]) for s in clip["samplers"])
        times = np.linspace(0,duration,round(duration*120)+1)
        pose = rig.pose(clip,times)
        root = rig.names["root"]
        pose[root]["translation"] = np.tile(rig.nodes[root].get("translation",[0,0,0]),(len(times),1))
        matrices = transforms(rig.world(pose))
        records = {}
        for side,(parts,markers) in sides.items():
            skinned = [vertices(matrices,piece) for piece in parts]
            lowest = np.min([p[:,:,1].min(axis=1) for p in skinned],axis=0)
            for position,(part,idx,old,up,axis,label) in markers.items():
                tri = skinned[part][:,idx]
                rotated = basis(tri) @ old.T
                normal = rotated @ up
                tilt = np.degrees(np.arccos(np.clip(normal @ up,-1,1)))
                # 先除去把脚长轴转到当前方向的最短旋转，再看绕脚长轴剩下的旋转（twist）。
                q = Rotation.from_matrix(rotated).as_quat()
                roll = abs((np.degrees(2*np.arctan2(q[:,:3]@axis,q[:,3]))+180)%360-180)
                phases = {}
                for phase,mask in [("shoe_near_ground",lowest<.03),("triangle_near_ground",tri[:,:,1].min(axis=1)<.03),
                                   ("swing",lowest>=.03)]:
                    if mask.any():
                        group = np.flatnonzero(mask)
                        worst = group[tilt[mask].argmax()]
                        phases[phase] = dict(samples=int(mask.sum()),max_tilt_deg=float(tilt[worst]),
                            max_axial_twist_deg=float(roll[mask].max()),inverted_samples=int(np.sum(normal[mask,1]<0)),
                            worst_seconds=float(times[worst]),lowest_shoe_at_worst_m=float(lowest[worst]))
                records[side+"_"+position] = dict(triangle=label,standing_up=up.tolist(),min_y_m=float(lowest.min()),phases=phases)
        output["clips"][name]=records
    return output


if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model",type=Path,default=ROOT/"RedflagForge/assets/game/station/crew/Operator.glb")
    parser.add_argument("--output",type=Path,default=ROOT/"output/validation/station-actions/soles.json")
    args=parser.parse_args()
    report = check(args.model)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2)+"\n",encoding="utf8")
    for clip, markers in report["clips"].items():
        for label, marker in markers.items():
            support = marker["phases"]["shoe_near_ground"]
            if support["inverted_samples"]:
                raise AssertionError(f"{clip} {label}: sole points upward during ground contact")
