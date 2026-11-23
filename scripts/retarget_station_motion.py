"""把 Quaternius 的交互动作转到 Godot 机器人的骨架。导入时使用 NumPy、SciPy，运行游戏无需它们。"""
import json
import struct

import numpy as np
from scipy.spatial.transform import Rotation, Slerp


# 源剪辑、游戏剪辑。同一份表也写入模型的来源清单。
GESTURES = [("Interact", "Station-Give"), ("Punch_Jab", "Station-Punch"),
            ("Crouch_Idle_Loop", "Station-Crouch"), ("Crouch_Fwd_Loop", "Station-CrouchWalk")]

CARE = [("Consume", "Station-UseMedkit"), ("Farm_PlantSeed", "Station-Revive"),
        ("LayToIdle", "Station-GetUp"), ("LayToIdle", "Station-Down")]


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
        width = {"SCALAR": 1, "VEC3": 3, "VEC4": 4, "MAT4": 16}[accessor["type"]]
        element = np.dtype({5121: "u1", 5123: "<u2", 5126: "<f4"}[accessor["componentType"]])
        offset = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
        return np.ndarray((accessor["count"], width), dtype=element, buffer=self.data, offset=offset,
                          strides=(view.get("byteStride", width * element.itemsize), element.itemsize))

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

    def skin_vertices(self, shoes_only):
        """保存蒙皮顶点的关节坐标和权重。站着时只需测靴底，躺下时还要测背甲和手臂。"""
        vertices = []
        for node in self.nodes:
            if "skin" not in node:
                continue
            skin = self.doc["skins"][node["skin"]]
            inverse_bind = self.values(skin["inverseBindMatrices"]).reshape(-1, 4, 4).transpose(0, 2, 1)
            joints = np.array(skin["joints"])
            shoes = [i for i in joints if self.nodes[i]["name"].startswith(("foot.", "toe.", "footcover."))]
            for primitive in self.doc["meshes"][node["mesh"]]["primitives"]:
                attrs = primitive["attributes"]
                weights = self.values(attrs["WEIGHTS_0"])
                indices = self.values(attrs["JOINTS_0"])
                keep = (np.any(np.isin(joints[indices], shoes) & (weights > 0), axis=1)
                        if shoes_only else np.ones(len(weights), dtype=bool))
                if not np.any(keep):
                    continue
                indices, weights = indices[keep], weights[keep]
                position = np.column_stack((self.values(attrs["POSITION"])[keep], np.ones(keep.sum())))
                bound = np.einsum("vkij,vj->vki", inverse_bind[indices], position)
                vertices.append((joints[indices], weights, bound))
        return vertices

    def floor_height(self, world, vertices):
        # 只计算世界坐标 Y，和引擎对顶点使用的蒙皮矩阵、四个关节权重一致。
        frames = max(len(pos) for pos, rotation, scale in world.values())
        row = np.empty((frames, len(self.nodes), 4))
        for i, (pos, rotation, scale) in world.items():
            row[:, i, :3] = rotation.as_matrix()[:, 1, :] * scale
            row[:, i, 3] = pos[:, 1]
        floor = np.full(frames, np.inf)
        for indices, weights, bound in vertices:
            height = np.einsum("fvki,vki,vk->fv", row[:, indices], bound, weights)
            floor = np.minimum(floor, height.min(axis=1))
        return floor


def align_standing_clips(doc, data, reference, clips, pelvis, movement_root):
    """让原地动作围绕走路时的同一个落脚点播放，保留骨盆起伏和关键帧之间的运动。"""
    rig = Rig(doc, data)
    joint, root = rig.names[pelvis], rig.names[movement_root]

    def pinned_pose(clip, times):
        local = rig.pose(clip, times)
        local[root]["translation"][:] = rig.nodes[root].get("translation", [0, 0, 0])
        return rig.world(local)

    # 一个完整步态的平均位置不会偏向某一只支撑脚。GLB 使用米，Y 是高度。
    walking = rig.clip(reference)
    end = max(rig.values(s["input"])[-1, 0] for s in walking["samplers"])
    center = pinned_pose(walking, np.linspace(0, end, 60, endpoint=False))[joint][0].mean(axis=0)
    binary = bytearray(data)
    for name in clips:
        clip = rig.clip(name)
        channel = next(c for c in clip["channels"] if c["target"] == {"node": joint, "path": "translation"})
        sampler = dict(clip["samplers"][channel["sampler"]])
        times = rig.values(sampler["input"])[:, 0]
        world = pinned_pose(clip, times)
        distance = center - world[joint][0][0]
        distance[1] = 0

        # GLB 中骨盆的平移写在父骨骼自己的坐标系里。
        _, rotation, scale = world[rig.parents[joint]]
        local_distance = rotation.inv().apply(np.tile(distance, (len(times), 1))) / scale
        values = (rig.values(sampler["output"]) + local_distance).astype("<f4")
        view = len(doc["bufferViews"])
        doc["bufferViews"].append(dict(buffer=0, byteOffset=len(binary), byteLength=values.nbytes))
        binary.extend(values.tobytes())
        sampler["output"] = len(doc["accessors"])
        doc["accessors"].append(dict(bufferView=view, componentType=5126, count=len(values), type="VEC3",
                                     min=values.min(axis=0).tolist(), max=values.max(axis=0).tolist()))
        channel["sampler"] = len(clip["samplers"])
        clip["samplers"].append(sampler)
    return bytes(binary)


def add_interactions(doc, data, gestures, floor_motion):
    dest = Rig(doc, data)
    idle = dest.pose(dest.clip("Idle-cycle"), [0])
    rest = dest.world(idle)
    shoes = dest.skin_vertices(shoes_only=True)
    whole_body = dest.skin_vertices(shoes_only=False)
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

    clips = [(floor_motion, "Farm_Harvest", "Station-Pickup")]
    clips.extend((gestures, original, name) for original, name in GESTURES)
    clips.append((gestures, "Fixing_Kneeling", "Station-Repair"))
    clips.extend((floor_motion, original, name) for original, name in CARE)
    for source_path, clip_name, robot_clip in clips:
        picking = robot_clip == "Station-Pickup"
        player_motion = robot_clip not in ("Station-Give", "Station-Pickup")
        source = Rig(*read_glb(source_path))
        joints, directions = mapping.copy(), ends.copy()
        if picking or player_motion:
            joints["hips"] = "pelvis"
            for side in ("L", "R"):
                for robot, person in (("thigh", "thigh"), ("knee", "calf"), ("foot", "foot")):
                    joints[robot + "." + side] = person + "_" + side.lower()
                directions["thigh." + side] = "knee." + side, "calf_" + side.lower()
                directions["knee." + side] = "foot." + side, "foot_" + side.lower()
                directions["foot." + side] = "toe." + side, "ball_" + side.lower()
        if player_motion:
            directions.update({"hips": ("spine1", "spine_01"), "spine1": ("spine2", "spine_02"),
                               "spine2": ("chest", "spine_03"), "chest": ("neck", "neck_01"),
                               "neck": ("head.001", "Head")})
        if robot_clip == "Station-Punch":
            # 用源动画指节的方向重建握拳。只移动手腕会让原模型张着手掌向前伸。
            for side in ("L", "R"):
                for robot, finger in (("f_index", "index"), ("f_middle", "middle"), ("f_ring", "ring"), ("thumb", "thumb")):
                    for part in (1, 2):
                        name = f"{robot}.0{part}.{side}"
                        joints[name] = f"{finger}_0{part}_{side.lower()}"
                        directions[name] = f"{robot}.0{part+1}.{side}", f"{finger}_0{part+1}_{side.lower()}"
        original = source.clip(clip_name)
        duration = max(source.values(s["input"])[-1, 0] for s in original["samplers"])
        # 原动作以 30 Hz 采样。保留同一秒数和帧数，关键帧之间由引擎做球面插值。
        times = np.linspace(0, duration, round(float(duration) * 30) + 1)
        original_world = source.world(source.pose(original, times))
        seconds = {"Station-UseMedkit": 2., "Station-Repair": 2.6, "Station-Revive": 2.6, "Station-Down": 1.}
        if robot_clip in seconds:
            times = np.linspace(0, seconds[robot_clip], len(times))
        def source_bone(name):
            if not picking and robot_clip != "Station-Revive":
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
                    before = [before / np.linalg.norm(before)]
                    after = [after / np.linalg.norm(after)]
                    # 一条骨能定伸展方向，第二个方向定转身或翻掌的角度。
                    if player_motion and name in ("hips", "spine1", "spine2", "chest"):
                        before.append(rest[dest.names["upper_arm.L"]][0][0] - rest[dest.names["upper_arm.R"]][0][0])
                        after.append(source_bone("upperarm_l")[0][0] - source_bone("upperarm_r")[0][0])
                    elif player_motion and name in ("hand.L", "hand.R"):
                        side = name[-1]
                        before.append(rest[dest.names["f_index.01." + side]][0][0] - rest[dest.names["f_ring.01." + side]][0][0])
                        after.append(source_bone("index_01_" + side.lower())[0][0] - source_bone("ring_01_" + side.lower())[0][0])
                    if len(before) == 2:
                        before[1] /= np.linalg.norm(before[1])
                        after[1] /= np.linalg.norm(after[1])
                    correction, _ = Rotation.align_vectors(after, before)
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
            if player_motion and name == "hips":
                # 人与机器人的腿长不同。按大腿与小腿的总长缩放骨盆轨迹，接着由双脚决定离地高度。
                def leg(lookup, names):
                    hip, knee, foot = [lookup(j) for j in names]
                    return np.linalg.norm(hip-knee) + np.linalg.norm(knee-foot)
                robot_length = leg(lambda j: rest[dest.names[j]][0][0], ("thigh.L", "knee.L", "foot.L"))
                human_length = leg(lambda j: source_bone(j)[0][0], ("thigh_l", "calf_l", "foot_l"))
                displacement = source_bone("pelvis")[0] * (robot_length / human_length) - rest[i][0][0]
                result[i]["translation"] = idle[i]["translation"] + parent.inv().apply(displacement) / rest[dest.parents[i]][2]
        if picking or player_motion:
            posed = dest.world(result)
            delta = np.zeros((len(times), 3))
            if player_motion:
                # 蹲行会翻滚脚掌，只看脚尖会让脚跟穿地。用实际靴底最低的顶点贴住地面。
                resting_on_body = robot_clip in ("Station-Down", "Station-GetUp", "Station-Repair", "Station-Revive")
                delta[:, 1] = -dest.floor_height(posed, whole_body if resting_on_body else shoes)
            else:
                # 拾取动作保留原来的脚尖接触高度，手指随后也相对于同一地面定位。
                feet = [dest.names["toe.L"], dest.names["toe.R"]]
                ground = min(rest[i][0][0, 1] for i in feet)
                delta[:, 1] = ground - np.minimum(posed[feet[0]][0][:, 1], posed[feet[1]][0][:, 1])
            pelvis = dest.names["hips"]
            _, parent, scale = rest[dest.parents[pelvis]]
            result[pelvis]["translation"] += parent.inv().apply(delta) / scale

        if robot_clip == "Station-CrouchWalk":
            # 原动作在原地踏步。脚尖离最低点不到 2 厘米且向后移时，用它估计身体向前的速度。
            posed = dest.world(result)
            support_speeds = []
            for foot in ("toe.L", "toe.R"):
                toe = posed[dest.names[foot]][0]
                speed = -np.diff(toe[:, 2]) / np.diff(times)
                supporting = (toe[:-1, 1] < toe[:, 1].min() + .02) & (speed > 0)
                support_speeds.extend(speed[supporting])
            # 走路和跑步原件都把前进距离写在 root。游戏可按同一约定读取步幅，再固定 root 播放。
            root = dest.names["root"]
            _, parent, scale = posed[dest.parents[root]]
            travel = np.zeros((len(times), 3))
            travel[:, 2] = times * np.median(support_speeds)
            result[root]["translation"] = result[root]["translation"] + parent.inv().apply(travel) / scale

        if picking:
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
        if robot_clip == "Station-UseMedkit":
            from station_medkit_pose import fit_medkit
            fit_medkit(dest, result, times)

        for i in dest.order:
            for path, values in result[i].items():
                write(i, path, values[::-1] if robot_clip == "Station-Down" else values)
        doc["animations"].append(output)
    return bytes(binary)
