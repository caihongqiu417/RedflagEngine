"""用已知颜色、法线和扫描高度检查烘焙，并核对重复运行会保留手工资产。"""
from pathlib import Path
import json
import sys
from tempfile import TemporaryDirectory

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"scripts"))
from bake_surface_maps import bake, bake_image, digest


with TemporaryDirectory() as temp:
    folder = Path(temp)
    color, normal, height = (folder/f for f in ("color.png","normal.png","height.png"))
    Image.fromarray(np.array([[0,255],[0,255]], dtype="uint8")).convert("RGB").save(color)
    Image.fromarray(np.array([[[255,128,255],[0,127,255]]]*2, dtype="uint8")).save(normal)
    Image.fromarray(np.array([[32768,32768],[33000,33000]], dtype="uint16")).save(height)
    assert bake_image(color,"base_color",[1,1]).getpixel((0,0)) == (188,188,188)
    assert bake_image(normal,"normal_gl",[1,1]).getpixel((0,0)) == (128,128,255)
    assert bake_image(height,"height",[1,1]).getpixel((0,0)) == round((32768+33000)/2/65535*255)
    recipe = folder/"rack.json"
    files = {k:dict(file=p.name,md5=digest(p,"md5"),url=p.as_uri())
             for k,p in (("base_color",color),("normal_gl",normal),("height",height))}
    recipe.write_text(json.dumps(dict(source="fixture",license="CC0",size=[1,1],maps=files)),encoding="utf8")
    target = folder/"bake"
    receipt = bake(recipe,folder,target)
    recipe.write_bytes(json.dumps(json.loads(recipe.read_bytes()),indent=2).replace("\n","\r\n").encode())
    assert receipt == bake(recipe,folder,target) == bake(recipe,folder,folder/"independent")
    (target/"Normal.png").write_bytes(b"artist edit")
    try:
        bake(recipe,folder,target)
    except ValueError:
        assert (target/"Normal.png").read_bytes() == b"artist edit"
    else:
        raise AssertionError("不应覆盖手改的法线")
    height.write_bytes(b"incomplete input")
    try:
        bake(recipe,folder,folder/"incomplete")
    except ValueError:
        assert not (folder/"incomplete").exists()
    else:
        raise AssertionError("输入损坏时不能发布其余贴图")
print("底色、法线与高度通过，独立烘焙一致，保留手改和上次成功结果。")
