"""读取 SceneGallery 的 RSM 开关对照附件，输出可复核的线性数值。"""
import json
import sys
from pathlib import Path
import numpy as np
import yaml

folder = Path(sys.argv[1])
report = {'source': str(folder), 'coordinates': 'top-left pixel origin; world-space positions', 'cases': []}


def load(settings, attachment):
    data = settings['Result']['Attachments'][attachment]
    return np.fromfile(folder / data['File'], dtype='<f4').reshape(data['Height'], data['Width'], data['Channels'])


def world_coordinates(settings, depth, normal):
    eye = np.array(settings['Eye'], dtype=float)
    forward = np.array(settings['Target'], dtype=float) - eye
    forward /= np.linalg.norm(forward)
    right = np.cross(forward, [0, 1, 0])
    right /= np.linalg.norm(right)
    up = np.cross(right, forward)
    view = np.eye(4)
    view[:3, :3] = [right, up, -forward]
    view[:3, 3] = -view[:3, :3] @ eye
    width, height = settings['Size']
    near, far = settings['Near'], settings['Far']
    cot = 1 / np.tan(np.deg2rad(settings['Fov']) / 2)
    projection = np.array([[cot * height / width, 0, 0, 0], [0, cot, 0, 0],
                           [0, 0, -(far + near) / (far - near), -2 * far * near / (far - near)],
                           [0, 0, -1, 0]])
    y, x = np.mgrid[:height, :width]
    clip = np.stack((2 * (x + .5) / width - 1, 1 - 2 * (y + .5) / height,
                     depth[:, :, 0] * 2 - 1, np.ones_like(x)), axis=-1)
    world = clip @ np.linalg.inv(projection @ view).T
    return world[:, :, :3] / world[:, :, 3:4], normal[:, :, :3] @ view[:3, :3]


cases = ['Cornell-Original', 'Cornell-Red', 'Bounce']
if (folder / 'Cornell-Display-On.yaml').exists():
    cases.append('Cornell-Display')
for name in cases:
    on = yaml.safe_load((folder / (name + '-On.yaml')).read_text())
    off = yaml.safe_load((folder / (name + '-Off.yaml')).read_text())
    hdr_on, hdr_off = load(on, 'HDR'), load(off, 'HDR')
    resolved = load(on, 'RSMResolved')
    delta = hdr_on - hdr_off
    depth = load(on, 'Depth')
    world, normal = world_coordinates(on, depth, load(on, 'ViewNormal'))
    floor = (normal[:, :, 1] > .99) & (depth[:, :, 0] < 1) & (world[:, :, 1] < -1.9)
    row = {'name': name, 'direction': on['Direction'], 'intensity': on['RSMIntensity'],
           'frames': on['Frames'], 'delta_rgb_max': delta[:, :, :3].max(axis=(0, 1)).tolist(),
           'pixels': [], 'floor_regions': [], 'rsm_reflectors': []}
    for label, point in [('near_red', [-1.5, -2, 1]), ('near_green_or_far', [1.5, -2, 1])]:
        distance = np.linalg.norm(world - point, axis=-1)
        distance[~floor] = np.inf
        y, x = np.unravel_index(np.argmin(distance), distance.shape)
        row['pixels'].append({'label': label, 'x': int(x), 'y': int(y), 'world': world[y, x].tolist(),
                              'hdr_off': hdr_off[y, x, :3].tolist(), 'hdr_on': hdr_on[y, x, :3].tolist(),
                              'delta': delta[y, x, :3].tolist(), 'resolved': resolved[y, x, :3].tolist()})
        region = floor & (np.abs(world[:, :, 0] - point[0]) < .3) & (world[:, :, 2] > .4)
        row['floor_regions'].append({'label': label, 'pixels': int(region.sum()),
                                     'mean_delta': delta[region, :3].mean(axis=0).tolist(),
                                     'mean_resolved': resolved[region, :3].mean(axis=0).tolist()})
    flux, position, vpl_normal = load(on, 'RSMFlux'), load(on, 'RSMWorldPosition'), load(on, 'RSMNormal')
    for label, wall in [('red', position[:, :, 0] < -1.8), ('green', position[:, :, 0] > 1.8)]:
        wall &= (position[:, :, 3] > .5) & (np.abs(vpl_normal[:, :, 0] - .5) > .49)
        row['rsm_reflectors'].append({'wall': label, 'pixels': int(wall.sum()),
                                      'mean_flux': flux[wall, :3].mean(axis=0).tolist() if wall.any() else None})
    assert np.isfinite(delta).all() and np.isfinite(resolved).all()
    near, far = row['pixels']
    if name == 'Cornell-Original':
        assert row['rsm_reflectors'][0]['mean_flux'] == [0, 0, 0]
        assert far['delta'][1] > far['delta'][0], 'Lit green wall should add green light'
    else:
        assert near['delta'][0] > 1.5 * near['delta'][1], 'Lit red wall should add red light'
    report['cases'].append(row)

(folder / 'analysis.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps(report, indent=2))
