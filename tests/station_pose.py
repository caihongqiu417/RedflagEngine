"""旧帧直接记录剪辑；使用蒙太奇的帧另存各层，骨骼位置始终是实际呈现的结果。"""


def visible_pose(actor, slot="FullBody"):
    animation = actor["animation"]
    layer = max((m for m in animation.get("montages", ()) if m["slot"] == slot and m["weight"] > 0),
                key=lambda m: m["weight"], default=None)
    return dict(animation, **layer) if layer else animation
