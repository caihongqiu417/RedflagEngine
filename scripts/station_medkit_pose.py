"""将 Quaternius 的举手动作贴合维修员胸前的装甲；只在导入 GLB 时运行。"""
import numpy as np
from scipy.spatial.transform import Rotation


# Robot_Body 第 12275 号顶点在 chest 骨下的位置。药包前后厚 8.07 厘米。
PANEL = np.array([0., .111770, .236446])
HALF_DEPTH = .04035


def unit(v):
    return v / np.linalg.norm(v, axis=1, keepdims=True)


def turn(before, after):
    return Rotation.from_quat([Rotation.align_vectors([b], [a])[0].as_quat()
                               for a, b in zip(unit(before), unit(after))])


def fit_medkit(rig, pose, times):
    source = rig.world(pose)
    chest, upper, forearm, hand, finger = (rig.names[name] for name in
        ("chest", "upper_arm.L", "forearm.L", "hand.L", "f_middle.01.L"))
    chest_position, chest_rotation, chest_scale = source[chest]
    front = chest_rotation.apply([0, 0, 1])
    surface = chest_position + chest_rotation.apply(chest_scale * PANEL)
    center = surface + HALF_DEPTH * front

    shoulder, elbow, wrist, grip = [source[i][0] for i in (upper, forearm, hand, finger)]
    # 保留作者抬手、停顿、放手的速度。把手的终点从嘴前换到胸甲，离开时沿胸前原路放低。
    middle = len(times)//2
    lift = np.maximum(0., wrist[:,1] - wrist[0,1])
    lift[:middle] = np.maximum.accumulate(lift[:middle])
    lift[middle:] = np.maximum.accumulate(lift[middle:][::-1])[::-1]
    # 腕抬到原片最高点的 95% 就贴紧。这个比例决定药包在胸甲上停留多久。
    weight = np.minimum(lift / (.95*lift.max()), 1.)[:, None]
    body_turn = chest_rotation * chest_rotation[0].inv()
    resting_grip = chest_position + body_turn.apply(grip[0]-chest_position[0])
    # 途中略向前绕过装甲边缘，手持的包有四厘米厚。
    center_path = resting_grip + weight*(center-resting_grip) + 4*weight*(1-weight)*HALF_DEPTH*front

    # 手指横过胸前时，按手腕与握点的真实距离反求腕的位置。
    palm = grip - wrist
    neutral_hand = body_turn * source[hand][1][0]
    neutral_palm = (neutral_hand * source[hand][1].inv()).apply(palm)
    across = chest_rotation.apply([-.92, -.20, -.34])
    hand_turn = Rotation.from_rotvec(weight * turn(neutral_palm, across).as_rotvec())
    goal = center_path - hand_turn.apply(neutral_palm)

    # 用上臂和前臂的原长求肘位置。手碰到胸前时肘朝身体左侧弯，腕骨的朝向另行保留。
    axis = unit(goal-shoulder)
    length = np.linalg.norm(goal-shoulder, axis=1, keepdims=True)
    a = np.linalg.norm(elbow-shoulder, axis=1, keepdims=True)
    b = np.linalg.norm(wrist-elbow, axis=1, keepdims=True)
    along = (a*a - b*b + length*length)/(2*length)
    side = chest_rotation.apply([1, -.65, .08])
    pole = (1-weight)*unit(elbow-shoulder) + weight*side
    perpendicular = unit(pole - axis*np.sum(pole*axis, axis=1, keepdims=True))
    elbow_goal = shoulder + axis*along + perpendicular*np.sqrt(np.maximum(0, a*a-along*along))

    def rotate(node, world_rotation, previous_world):
        parent = previous_world[rig.parents[node]][1]
        pose[node]["rotation"] = (parent.inv()*world_rotation).as_quat()

    rotate(upper, turn(elbow-shoulder, elbow_goal-shoulder)*source[upper][1], source)
    moved = rig.world(pose)
    rotate(forearm, turn(moved[hand][0]-moved[forearm][0], goal-moved[forearm][0])*moved[forearm][1], moved)
    rotate(hand, hand_turn*neutral_hand, rig.world(pose))
