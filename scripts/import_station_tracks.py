"""将两张实物扫描按米铺到弯曲的车辙上。请先运行 import_station_ground.py 准备相邻路面的贴图。"""
from io import BytesIO
import json

import cv2
import numpy as np
from PIL import Image

from fetch_station_props import CACHE, OUT, read_url, verified
from redflag_mcp.surface_strip import bake, curve


START, END = -14.0, 10.0
AXLE = 1.54          # 两只轮胎中心的距离，米。
HALF_BAND = 0.36     # 含两侧过渡到碎石的范围；实际胎印宽约 0.34 米。
SCAN_WIDTH = 2.25    # 作者实测的正方形扫描边长，米。
ATLAS_WIDTH, ATLAS_HEIGHT = 512, 4096


def route(x, lateral):
    """中心线的法向决定两道车辙的位置，弯道上两只轮胎仍保持相同的间距。"""
    angle = (x + 7.0) / 5.0
    centre = 1.30 + 1.15 * np.sin(angle) + 0.025 * (x + 7.0)
    slope = (1.15 / 5.0) * np.cos(angle) + 0.025
    inv = 1.0 / np.sqrt(1.0 + slope * slope)
    return x - lateral * slope * inv, centre + lateral * inv, slope * inv, inv


def sample(scan, u, v):
    rows, cols = scan.shape[:2]
    px, py = np.broadcast_arrays((u % 1.0) * cols - 0.5, (v % 1.0) * rows - 0.5)
    return cv2.remap(scan, px.astype('f'), py.astype('f'), cv2.INTER_LINEAR,
                     borderMode=cv2.BORDER_WRAP).astype('f')


def smooth(lo, hi, value):
    t = np.clip((value - lo) / (hi - lo), 0, 1)
    return t*t*(3 - 2*t)


def srgb(rgb):
    return np.where(rgb <= .04045, rgb/12.92, ((rgb+.055)/1.055)**2.4)


def encode(rgb):
    return np.where(rgb <= .0031308, rgb*12.92, 1.055*rgb**(1/2.4)-.055)


def write_maps(folder, mud, points):
    gravel = OUT/'ground'
    dry = {role:np.asarray(Image.open(next(gravel.glob(pattern))))
           for role, pattern in [('color','*_diff_*.jpg'), ('normal','*_nor_gl_*.jpg'),
                                 ('orm','*_orm_*.png')]}
    images = {key:np.empty((ATLAS_HEIGHT, ATLAS_WIDTH, 3), 'uint8') for key in dry}
    gravel_height = np.asarray(Image.open(next(gravel.glob('*_disp_*.png'))))
    opacity = np.empty((ATLAS_HEIGHT, ATLAS_WIDTH), 'uint8')
    tile = ATLAS_WIDTH // 2
    metres = np.r_[0,np.cumsum(np.linalg.norm(np.diff(points,axis=0),axis=1))]
    distance = metres[-1]*(np.arange(ATLAS_HEIGHT)[:,None]+.5)/ATLAS_HEIGHT
    x = np.interp(distance,metres,np.asarray(points)[:,0])
    for side in (0, 1):
        across = ((np.arange(tile)[None,:] + .5)/tile * 2 - 1) * HALF_BAND
        wx, wz, sin, cos = route(x, across + (side-.5)*AXLE)
        du, dv = (wx-9)/2, -wz/2  # 与原碎石地面的两米 UV 完全相同。
        u, v = (.165, .555)[side] + across/SCAN_WIDTH, (x-START)/SCAN_WIDTH + .37*side
        height = sample(mud['Displacement'], u, v)/65535

        # 胎纹高处还能看见碎石。沿路保留泥土深浅与碎石露出的变化，车辙两端逐渐消失。
        shoulder = np.abs(across) + .065*(height-.25)
        edge = 1-smooth(.13, .31, shoulder)
        imprint = 1-smooth(.22, .49, height)
        ends = smooth(START, START+1.4, x) * (1-smooth(END-1.8, END, x))
        weight = edge * ends * (.28 + .65*imprint)
        w = weight[:,:,None]

        # 胎印边上略高的碎石仍露在原处，边缘随石头外形断开。灰度供 glTF 的透明裁剪读取。
        stones = sample(gravel_height,du,dv)/65535
        exposed = smooth(.16, .30, shoulder)
        alpha = np.clip(.9 - exposed*.9 - (1-ends)*.9 + .65*(.48-stones), 0, 1)
        opacity[:,side*tile:(side+1)*tile] = np.uint8(alpha*255+.5)

        original = srgb(sample(dry['color'],du,dv)/255)
        soil = srgb(sample(mud['Diffuse'],u,v)/255)
        color = encode(original*(1-w) + soil*w)

        normal = sample(dry['normal'],du,dv)/127.5-1
        nx, ny, nz = normal[:,:,0], normal[:,:,1], normal[:,:,2]
        aligned = np.stack((-nx*sin+ny*cos, -nx*cos-ny*sin, nz), axis=2)
        scanned = sample(mud['nor_gl'], u,v)/127.5-1
        mixed = aligned*(1-w) + scanned*w
        normal = (mixed / np.linalg.norm(mixed, axis=2, keepdims=True) + 1)/2

        orm = sample(dry['orm'],du,dv)/255
        rough = sample(mud['Rough'],u,v)/255
        orm[:,:,1] = orm[:,:,1]*(1-weight) + rough*weight
        for role, data in [('color',color), ('normal',normal), ('orm',orm)]:
            images[role][:,side*tile:(side+1)*tile] = np.uint8(np.clip(data,0,1)*255+.5)

    color = Image.fromarray(images['color'])
    color.putalpha(Image.fromarray(opacity))
    color.save(folder/'Earth.png')
    for key, name in [('normal','Normal.jpg'),('orm','Surface.png')]:
        Image.fromarray(images[key]).save(folder/name,quality=93,subsampling=0)


def main():
    folder = OUT/'tracks'
    folder.mkdir(parents=True,exist_ok=True)
    CACHE.mkdir(parents=True,exist_ok=True)
    info = json.loads(read_url('https://api.polyhaven.com/files/muddy_tracks'))
    sources, mud = {}, {}
    for role in ('Diffuse','nor_gl','Rough','Displacement'):
        ref = info[role]['2k']['png' if role=='Displacement' else 'jpg']
        mud[role] = np.asarray(Image.open(BytesIO(verified(ref))))
        sources[role] = {key:ref[key] for key in ('url','md5')}
    profile = folder/'Tracks.rfstrip'
    data = json.loads(profile.read_bytes())
    write_maps(folder,mud,curve(data['points'],data['subdivisions']))
    bake(profile)
    manifest = dict(author='Amal Kumar',license='CC0-1.0',source='https://polyhaven.com/a/muddy_tracks',
                    scan_m=SCAN_WIDTH,axle_m=AXLE,tread_width_m=.34,files=sources,
                    adjacent_gravel='../ground/sources.json',importer='scripts/import_station_tracks.py')
    (folder/'sources.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf8')


if __name__ == '__main__':
    main()
