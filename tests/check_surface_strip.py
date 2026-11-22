"""用另一项目的单条路面和双条车辙检查米制尺寸、UV、稳定资源号和打包依赖。"""
import base64
import json
from pathlib import Path
import struct
import sys
from tempfile import TemporaryDirectory

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from asset_bundle import AssetBundle
from redflag_mcp.surface_strip import bake


def stream(model,number,fmt):
    raw=base64.b64decode(model['buffers'][0]['uri'].split(',')[1])
    view=model['bufferViews'][number]
    return list(struct.iter_unpack(fmt,raw[view['byteOffset']:view['byteOffset']+view['byteLength']]))


def run(project,bands,repeat):
    project.mkdir()
    data=dict(points=[[0,0],[4,0],[4,3]],bands=bands,repeat_m=repeat,color='surface.png')
    (project/'surface.png').write_bytes((ROOT/'RedflagForge/assets/textures/decals/paint.png').read_bytes())
    source=project/'Trail.rfstrip'
    source.write_text(json.dumps(data))
    result=bake(source)
    meta=project/'Trail.gltf.rfmeta'
    meta.write_bytes(b'RedflagAsset 1\n59342 1\n')
    doc=json.loads(result.read_bytes())
    vertices, uv, indices=(stream(doc,i,fmt) for i,fmt in ((0,'<3f'),(2,'<2f'),(3,'<I')))
    assert len(indices)==len(bands)*12 and max(indices)[0]<len(vertices)
    for i,band in enumerate(bands):
        pos=i*6
        assert abs(vertices[pos+1][2]-vertices[pos][2]-band['width'])<1e-5
        assert abs((vertices[pos+1][2]+vertices[pos][2])/2-band.get('offset',0))<1e-5
        assert abs(uv[pos+5][1]-7/(repeat or 7))<1e-5
    bundle=AssetBundle(project)
    bundle.add(result,'test scene')
    assert {'Trail.gltf','Trail.gltf.rfmeta','surface.png'}==set(bundle.files)
    initial=result.read_bytes()
    assert bake(source).read_bytes()==initial and b'59342' in meta.read_bytes()
    (project/'surface.png').unlink()
    try: bake(source)
    except ValueError: pass
    else: raise AssertionError('missing source texture should fail the bake')
    assert result.read_bytes()==initial


if __name__=='__main__':
    with TemporaryDirectory() as directory:
        root=Path(directory)
        run(root/'footpath',[dict(width=1.8)],2)
        run(root/'two-bands',[dict(width=.32,offset=-.7),dict(width=.32,offset=.7)],0)
    project=ROOT/'RedflagForge'
    bundle=AssetBundle(project)
    bundle.scene(project/'assets/scenes/Watt-Station.redflag')
    folder='assets/game/station/salvage/tracks/'
    packed={name[len(folder):] for name in bundle.files if name.startswith(folder)}
    assert {'Tracks.gltf','Tracks.gltf.rfmeta','Earth.png','Normal.jpg','Surface.png'}<=packed
    print('Two projects, widths, bends, UV, safe rebuild and station dependencies passed.')
