"""生成固定灯光/相机的 RSM 开关对照配置，不改参考场景。"""
import os
import sys
from pathlib import Path
import yaml

root = Path(__file__).resolve().parent.parent
output = Path(sys.argv[1]).resolve()
output.mkdir(parents=True, exist_ok=True)
base = dict(Model=os.path.relpath(root / 'RedflagForge/assets/models/reference/CornellBox/CornellBox.obj', output),
            Output='.', Readback=True, Size=[1024, 768], ModelSize=4, ModelRotation=[0, 0, 0],
            Eye=[0, 0, 7.8], Target=[0, 0, 0], Fov=45, Near=.05, Far=100,
            LightIntensity=3, PointPosition=[0, 1.4, 0], PointIntensity=1, PointRadius=6,
            ShadowHalfSize=4, SSAO=True, RSMIntensity=5, RSMRadius=.4, Exposure=1, Frames=12)
cases = [('Cornell-Original', dict(Direction=[.25, -.6, -1])),
         ('Cornell-Red', dict(Direction=[-.6, -.6, -1])),
         ('Cornell-Display', dict(Direction=[-.6, -.6, -1], RSMIntensity=1)),
         ('Bounce', dict(Model=os.path.relpath(root / 'tests/fixtures/rsm/Bounce.obj', output),
                         Size=[512, 512], Eye=[4, 3, 6], Target=[0, -.5, 0], Direction=[-1, -1, -.3],
                         PointIntensity=0, SSAO=False, ShadowHalfSize=3, RSMIntensity=1,
                         Frames=1, RSMBlendAlpha=0))]
for case, changes in cases:
    for label, enabled in [('Off', False), ('On', True)]:
        name = case + '-' + label
        settings = dict(base, **changes)
        settings.update(Name=name, RSM=enabled)
        (output / (name + '.yaml')).write_text(yaml.safe_dump(settings, sort_keys=False), encoding='utf-8')
