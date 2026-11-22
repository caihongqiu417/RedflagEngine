"""把 Quaternius 的交互动作转到 Godot 机器人的骨架。导入时使用 NumPy、SciPy，运行游戏无需它们。"""
import json
import struct

import numpy as np
from scipy.spatial.transform import Rotation, Slerp


def read_glb(path):
    raw = path.read_bytes()
    size = struct.unpack_from("<I", raw, 12)[0]
    return json.loads(raw[20:20 + size]), raw[28 + size:]


class Rig:
    def __init__(self, doc, data):
        self.doc, self.data = doc, data
        self.nodes = doc["nodes"]
        self.names = {node["name"]: i for i, node in enumerate(self.nodes)}
        self.parents = {child: i for i, node in enumerate(self.nodes) for child in node.get("children", [])}
        self.order = []

        def visit(i):
            self.order.append(i)
            for child in self.nodes[i].get("children", []):
                visit(child)
        for i in range(len(self.nodes)):
            if i not in self.parents:
                visit(i)

    def values(self, index):
        accessor = self.doc["accessors"][index]
        view = self.doc["bufferViews"][accessor["bufferView"]]
        width = {"SCALAR": 1, "VEC3": 3, "VEC4": 4}[accessor["type"]]
        offset = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
        return np.ndarray((accessor["count"], width), dtype="<f4", buffer=self.data, offset=offset,
                          strides=(view.get("byteStride", width * 4), 4))

    def clip(self, name):
        return next(a for a in self.doc["animations"] if a["name"] == name)

    def pose(self, clip, times):
        defaults = {"translation": [0, 0, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1]}
        local = [{k: np.tile(node.get(k, v), (len(times), 1)) for k, v in defaults.items()} for node in self.nodes]
        for channel in clip["channels"]:
            sampler = clip["samplers"][channel["sampler"]]
            if sampler.get("interpolation", "LINEAR") != "LINEAR":
                raise ValueError("源动作需要线性关键帧")
            target = channel["target"]
            key, values = self.values(sampler["input"])[:, 0], self.values(sampler["output"])
            sample = np.clip(times, key[0], key[-1])
            if len(key) == 1:
                result = np.tile(values[0], (len(times), 1))
            elif target["path"] == "rotation":
                result = Slerp(key, Rotation.from_quat(values))(sample).as_quat()
            else:
                result = np.stack([np.interp(sample, key, row) for row in values.T], axis=-1)
            local[target["node"]][target["path"]] = result
        return local

    def world(self, local):
        world = {}
        for i in self.order:
            tr = local[i]
            pos, rotation, scale = tr["translation"], Rotation.from_quat(tr["rotation"]), tr["scale"]
            if i in self.parents:
                p, r, s = world[self.parents[i]]
                pos, rotation, scale = p + r.apply(s * pos), r * rotation, s * scale
            world[i] = pos, rotation, scale
        return world


def add_interactions(doc, data, gestures, floor_motion):
    dest = Rig(doc, data)
    idle = dest.pose(dest.clip("Idle-cycle"), [0])
    rest = dest.world(idle)
    # 目标骨架多出的活塞、前臂滑轨和指节沿用作者待机时的局部姿势，会随父骨骼运动。
    mapping = {"spine1": "spine_01", "spine2": "spine_02", "chest": "spine_03",
               "neck": "neck_01", "head.001": "Head"}
    for side in ("L", "R"):
        for robot, person in (("shoulder", "clavicle"), ("upper_arm", "upperarm"),
                              ("forearm", "lowerarm"), ("hand", "hand")):
            mapping[robot + "." + side] = person + "_" + side.lower()
    ends = {}
    for side in ("L", "R"):
        for arm, child, source_child in (("upper_arm", "forearm", "lowerarm"),
                                         ("forearm", "hand", "hand"),
                                         ("hand", "f_middle.01", "middle_01")):
            ends[arm + "." + side] = child + "." + side, source_child + "_" + side.lower()
    binary = bytearray(data)

    def store(array, kind):
        array = np.ascontiguousarray(array, dtype="<f4")
        view = len(doc["bufferViews"])
        doc["bufferViews"].append({"buffer": 0, "byteOffset": len(binary), "byteLength": array.nbytes})
        binary.extend(array.tobytes())
        item = dict(bufferView=view, componentType=5126, count=len(array), type=kind)
        if kind == "SCALAR":
            item.update(min=[float(array.min())], max=[float(array.max())])
        doc["accessors"].append(item)
        return len(doc["accessors"]) - 1

    clips = [(floor_motion, "Farm_Harvest", "Station-Pickup"), (gestures, "Interact", "Station-Give")]
    for source_path, clip_name, robot_clip in clips:
        picking = robot_clip == "Station-Pickup"
        source = Rig(*read_glb(source_path))
        joints, directions = mapping.copy(), ends.copy()
        if picking:
            joints["hips"] = "pelvis"
            for side in ("L", "R"):
                for robot, person in (("thigh", "thigh"), ("knee", "calf"), ("foot", "foot")):
                    joints[robot + "." + side] = person + "_" + side.lower()
                directions["thigh." + side] = "knee." + side, "calf_" + side.lower()
                directions["knee." + side] = "foot." + side, "foot_" + side.lower()
                directions["foot." + side] = "toe." + side, "ball_" + side.lower()
        original = source.clip(clip_name)
        duration = max(source.values(s["input"])[-1, 0] for s in original["samplers"])
        # 原动作以 30 Hz 采样。保留同一秒数和帧数，关键帧之间由引擎做球面插值。
        times = np.linspace(0, duration, round(float(duration) * 30) + 1)
        original_world = source.world(source.pose(original, times))
        def source_bone(name):
            if not picking:
                return original_world[source.names[name]][:2]
            # 源角色右手采集，机器人的右臂带装备。交换左右骨并沿角色正中面对称，改由左手低身取物。
            if name.endswith(("_r", "_l")):
                name = name[:-1] + ("r" if name.endswith("_l") else "l")
            pos, rot, _ = original_world[source.names[name]]
            return pos * [-1, 1, 1], Rotation.from_quat(rot.as_quat() * [1, -1, -1, 1])
        output = {"name": robot_clip, "samplers": [], "channels": []}
        time_index = store(times, "SCALAR")
        static_time = store(np.array([0]), "SCALAR")

        def write(node, path, values):
            fixed = np.allclose(values, values[0], atol=1e-6)
            default = dest.nodes[node].get(path, {"rotation": [0, 0, 0, 1], "translation": [0, 0, 0], "scale": [1, 1, 1]}[path])
            if fixed and np.allclose(values[0], default, atol=1e-6):
                return
            item = store(values[:1] if fixed else values, "VEC4" if path == "rotation" else "VEC3")
            output["channels"].append({"sampler": len(output["samplers"]), "target": {"node": node, "path": path}})
            output["samplers"].append({"input": static_time if fixed else time_index, "output": item})

        rotations = {}
        result = [{k: v.copy() for k, v in node.items()} for node in idle]
        for i in dest.order:
            name = dest.nodes[i]["name"]
            parent = rotations.get(dest.parents.get(i), Rotation.identity())
            local = Rotation.from_quat(idle[i]["rotation"])
            if name in joints:
                start, motion = source_bone(joints[name])
                reference = rest[i][1]
                if name in directions:
                    # 两个模型都面向 +Z。两条手臂的待机姿势不同，先让上臂和前臂分别指向源角色的肘和腕。
                    dest_end, source_end = directions[name]
                    before = rest[dest.names[dest_end]][0][0] - rest[i][0][0]
                    after = source_bone(source_end)[0][0] - start[0]
                    correction, _ = Rotation.align_vectors([after / np.linalg.norm(after)], [before / np.linalg.norm(before)])
                    reference = correction * reference
                # 将源骨相对第一帧的转动施加到目标骨骼，再换回它在父骨骼下的旋转。
                world = motion * motion[0].inv() * reference
                local = parent.inv() * world
            rotations[i] = parent * local
            result[i]["rotation"] = local.as_quat()
            if picking and name == "hips":
                hip = source_bone("pelvis")[0]
                ankle = source_bone("foot_l")[0][0]
                tall = np.linalg.norm(rest[i][0][0] - rest[dest.names["foot.L"]][0][0])
                displacement = (hip - hip[0]) * (tall / np.linalg.norm(hip[0] - ankle))
                result[i]["translation"] = idle[i]["translation"] + parent.inv().apply(displacement) / rest[dest.parents[i]][2]
        if picking:
            # 两台角色的腿长不同。以机器人原来较低的脚尖为地面，逐帧消除重定向后双脚的上下漂移。
            feet = [dest.names["toe.L"], dest.names["toe.R"]]
            ground = min(rest[i][0][0, 1] for i in feet)
            posed = dest.world(result)
            delta = np.zeros((len(times), 3))
            delta[:, 1] = ground - np.minimum(posed[feet[0]][0][:, 1], posed[feet[1]][0][:, 1])
            pelvis = dest.names["hips"]
            _, parent, scale = rest[dest.parents[pelvis]]
            result[pelvis]["translation"] += parent.inv().apply(delta) / scale

            # 机器人的指节更长。在腕部调整伸手的角度，让指节留在地面上方，手腕仍走作者的路径。
            posed = dest.world(result)
            hand = dest.names["hand.L"]
            wrist, hand_world, _ = posed[hand]
            fingers = np.stack([posed[dest.names[n]][0] for n in
                                ("f_index.03.L", "f_middle.03.L", "f_ring.03.L", "thumb.03.L")], axis=1)
            tip = fingers[np.arange(len(times)), fingers[:, :, 1].argmin(axis=1)]
            turn = []
            for wrist_tip, pivot in zip(tip, wrist):
                clearance = ground + .060  # 指节中心离地 6 厘米，给模型指尖留出厚度。
                if wrist_tip[1] >= clearance:
                    turn.append(Rotation.identity().as_quat())
                    continue
                current = wrist_tip - pivot
                lifted = current.copy()
                lifted[1] = clearance - pivot[1]
                horizontal = np.sqrt(np.dot(current, current) - lifted[1]**2)
                lifted[[0, 2]] *= horizontal / np.linalg.norm(current[[0, 2]])
                rotation, _ = Rotation.align_vectors([lifted], [current])
                turn.append(rotation.as_quat())
            parent = posed[dest.parents[hand]][1]
            result[hand]["rotation"] = (parent.inv() * Rotation.from_quat(turn) * hand_world).as_quat()
        for i in dest.order:
            for path, values in result[i].items():
                write(i, path, values)
        doc["animations"].append(output)
    return bytes(binary)
