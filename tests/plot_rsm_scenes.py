"""用真实 HDR 开关差值画诊断图，并汇总修复前后同参数的采样值。"""
import csv
import json
import sys
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.font_manager import FontProperties
import numpy as np
import yaml

before, after, output = map(Path, sys.argv[1:4])
output.mkdir(parents=True, exist_ok=True)
font = FontProperties(fname='C:/Windows/Fonts/msyh.ttc')
on = yaml.safe_load((after / 'Cornell-Display-On.yaml').read_text())
off = yaml.safe_load((after / 'Cornell-Display-Off.yaml').read_text())


def hdr(settings):
    data = settings['Result']['Attachments']['HDR']
    return np.fromfile(after / data['File'], dtype='<f4').reshape(data['Height'], data['Width'], 4)[:, :, :3]


delta = hdr(on) - hdr(off)
display = np.clip(delta * 4, 0, 1) ** (1 / 2.2)
images = [plt.imread(after / 'Cornell-Display-Off.png'), display,
          plt.imread(after / 'Cornell-Display-On.png')]
titles = ['RSM 关闭：直接光与其他效果', '仅间接增量：线性 HDR 差值 ×4', 'RSM 开启：最终画面，强度 1']
figure, axes = plt.subplots(1, 3, figsize=(15, 4.8), facecolor='white')
for axis, image, title in zip(axes, images, titles):
    axis.imshow(image)
    axis.set_title(title, fontproperties=font, fontsize=13, pad=10)
    axis.axis('off')
    for x, label in [(307, 'A'), (716, 'B')]:
        axis.plot(x, 651, 'o', markersize=5, markerfacecolor='none', markeredgecolor='white')
        axis.text(x + 12, 646, label, color='white', fontsize=10, weight='bold')
figure.text(.5, .055, '同一相机、灯光与 12 帧累积；A=(307,651)，B=(716,651)，坐标从左上角开始。',
            ha='center', fontproperties=font, fontsize=11)
figure.text(.5, .015, '中图对正 HDR 差值乘 4 后截到 [0,1]，仅用 2.2 伽马显示；未降噪。左右图直接读取引擎最终纹理。',
            ha='center', fontproperties=font, fontsize=10, color='#444444')
figure.subplots_adjust(left=.008, right=.992, top=.93, bottom=.13, wspace=.025)
figure.savefig(output / 'Cornell-RSM.png', dpi=180)
plt.close(figure)

records = []
for stage, directory in [('before', before), ('after', after)]:
    report = json.loads((directory / 'analysis.json').read_text())
    (output / (stage + '-analysis.json')).write_text(json.dumps(report, indent=2), encoding='utf-8')
    for case in report['cases']:
        for pixel in case['pixels']:
            row = dict(stage=stage, case=case['name'], intensity=case['intensity'], frames=case['frames'],
                       label=pixel['label'], x=pixel['x'], y=pixel['y'])
            for field in ('hdr_off', 'hdr_on', 'delta', 'resolved'):
                row.update({field + '_' + channel: value for channel, value in zip('rgb', pixel[field])})
            records.append(row)
with (output / 'measurements.csv').open('w', newline='', encoding='utf-8') as stream:
    writer = csv.DictWriter(stream, fieldnames=list(records[0]))
    writer.writeheader()
    writer.writerows(records)
metadata = {'source': 'actual SceneGallery direct readbacks; not image generation or RenderDoc GUI screenshots',
            'before': str(before), 'after': str(after), 'display_case': 'Cornell-Display',
            'display_indirect_multiplier': 4, 'display_gamma': 2.2,
            'display_intensity': 1, 'same_camera_lights': True,
            'fixed_parameter_comparison_intensity': {'Cornell-Original': 5, 'Cornell-Red': 5, 'Bounce': 1},
            'raw_format': 'little-endian float32, top-left origin; files kept in diagnostic output folder'}
(output / 'figure-source.json').write_text(json.dumps(metadata, indent=2), encoding='utf-8')
