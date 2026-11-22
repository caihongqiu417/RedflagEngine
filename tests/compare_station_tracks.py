"""比较真实游戏机位上原来是碎石、加上车辙后仍可见的像素。"""
import hashlib
import json
from pathlib import Path
import sys

import numpy as np
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'output/validation/station-tracks'


def visible(work):
    before,after,final=[np.fromfile(work/(key+'-depth.f32'),np.float32).reshape(1080,1920)
                        for key in ('before','after','final')]
    return (before!=after)&(after==final)


def camera(work):
    frame=json.loads((work/'session/frames.jsonl').read_bytes().splitlines()[11])
    return next(actor['position'] for actor in frame['actors'] if actor['id']=='1100')


def main(exe):
    version=hashlib.sha256(exe.read_bytes()).hexdigest()
    labels=['tracks-original','tracks-current']
    work=ROOT/'tmp/station-tracks'/version[:12]
    positions=[camera(work/n) for n in labels]
    assert positions[0]==positions[1]
    floor,tracks=[visible(work/n) for n in labels]
    both=floor&tracks
    images=[Image.open(OUT/(n+'.png')).convert('RGB') for n in labels]
    strip=Image.new('RGB',(960,1080))
    for i,image in enumerate(images):
        strip.paste(image.resize((960,540),Image.Resampling.LANCZOS),(0,540*i))
    strip.save(OUT/'comparison.jpg',quality=93)
    old,new=[np.asarray(image).astype('f') for image in images]
    delta=(new-old)[both]
    result=dict(exe_sha256=version,camera=positions,original_floor_pixels=int(floor.sum()),
                new_track_pixels=int(tracks.sum()),common_pixels=int(both.sum()),
                fraction_of_original_floor=float(both.sum()/floor.sum()),rgb_abs_mean=float(np.abs(delta).mean()),
                mean_display_rgb_change=delta.mean(axis=0).tolist(),
                absolute_rgb_change_at_least_8=int((np.abs(delta).mean(axis=1)>=8).sum()))
    (OUT/'comparison.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
    print(result)


if __name__=='__main__':
    main(Path(sys.argv[1]))
