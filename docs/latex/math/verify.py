"""复算讲义中的数值；只检查数学例子，不代替 GPU 渲染测试。"""
from math import isclose, pi, sqrt
from pathlib import Path
import re


def close(actual, expected):
    assert all(isclose(a, b, abs_tol=1e-9) for a, b in zip(actual, expected)), (actual, expected)


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def multiply(matrix, vector):
    return [dot(row, vector) for row in matrix]


model = [[2, 0, 0, 3], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]
view = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, -3], [0, 0, 0, 1]]
projection = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, -1.25, -2.25], [0, 0, -1, 0]]
world = multiply(model, [.5, 1, -2, 1])
camera = multiply(view, world)
clip = multiply(projection, camera)
ndc = [value / clip[3] for value in clip[:3]]
close(world, [4, 1, -2, 1])
close(camera, [4, 1, -5, 1])
close(clip, [4, 1, 4, 5])
close(ndc, [.8, .2, .8])
print("Transform: world=(4,1,-2), view=(4,1,-5), clip=(4,1,4,5), depth=0.9")

tangent = [2 / sqrt(2), 1 / sqrt(2), 0]
wrong = [2 / sqrt(2), -1 / sqrt(2), 0]
normal = [.5 / sqrt(2), -1 / sqrt(2), 0]
close([dot(wrong, tangent), dot(normal, tangent)], [1.5, 0])
print("Normal: wrong dot=1.5; inverse-transpose dot=0")

roughness, metallic, albedo, radiance = .5, 0, .5, 3
alpha = roughness ** 2
d = alpha ** 2 / max(pi * (alpha ** 2) ** 2, .0001)
f = .04
specular = min(d * f / (4 + .0001), 80)
diffuse = (1 - f) * (1 - metallic) * albedo / pi
result = (diffuse + specular) * radiance
assert abs(result - .61115) < .00001
print(f"PBR: D={d:.6f}, specular={specular:.8f}, diffuse={diffuse:.8f}, output={result:.8f}")

# 此矩阵由正文 P 求逆，乘积再还原同一观察坐标。
inverse_projection = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 0, -1], [0, 0, -4/9, 5/9]]
homogeneous = multiply(inverse_projection, [*ndc, 1])
restored = [value / homogeneous[3] for value in homogeneous[:3]]
close(homogeneous, [.8, .2, -1, .2])
close(restored, camera[:3])
near, far = 1, 9
for depth, expected in [(0, -1), (.9, -5), (1, -9)]:
    close([-near * far / (far - depth * (far - near))], [expected])
print("Depth: inverse projection closes; near=-1, example=-5, far=-9")

# RSM 使用投影面积，发送与接收各有一次 Lambert 系数。
for light_cosine in [1, .5, .1]:
    surface_area = .02 / light_cosine
    close([.5 * 2 * light_cosine * surface_area], [.02])
for distance in [1, 2, 5]:
    normalized = .6 * .8 / distance ** 2
    displacement = (.6 * distance) * (.8 * distance) / distance ** 4
    close([normalized], [displacement])
width, uv_radius, density, geometry = 10, .2, 1, .25
resolved = (width * uv_radius) ** 2 / pi * density * geometry
close([resolved, .8 * resolved], [1 / pi, .8 / pi])
scaled = (2 * width * uv_radius) ** 2 / pi * density * (geometry / 4)
close([scaled], [resolved])
for count in [16, 64, 128]:
    radii_squared = [(i + .5) / count for i in range(count)]
    close([sum(radii_squared) / count], [.5])
print(f"RSM: density example={resolved:.8f}; receiver={.8 * resolved:.8f}; area/scale identities pass")

# 光照效果的小例子和正文使用同一组输入。
shadow_depths = [.70] * 6 + [.55] * 3
visibility = sum(.60 <= depth for depth in shadow_depths) / len(shadow_depths)
close([visibility, (1 - 1 / 4) ** 2], [2 / 3, .5625])

luminance, threshold = 2., 1.
knee = max(threshold / 2, .0001)
soft = min(max(luminance - threshold + knee, 0), 2 * knee)
contribution = max(luminance - threshold, soft ** 2 / (4 * knee + .0001), 0) / luminance
bright = luminance * contribution
close([bright / (1 + bright)], [.5])
x = 2.
tone = (x * (2.51 * x + .03)) / (x * (2.43 * x + .59) + .14)
assert round(tone, 5) == .91486 and round(255 * tone ** (1 / 2.2)) == 245
print("Effects: PCF=6/9; ambient visibility=0.5625; bright=0.5; display=245/255")

root = Path(__file__).resolve().parents[3]
source_cache = {}
excerpt_count = 0
for name in ["main.tex", "effects.tex", "rsm.tex", "rsm-validation.tex"]:
    document = Path(__file__).with_name(name).read_text(encoding="utf-8")
    # 保留断开的节选，每一组都只对照紧随其后的源码位置。带标题的旧版片段单独标有年代。
    pending = []
    entries = re.finditer(r"\\begin\{lstlisting\}(?!\[)(.*?)\\end\{lstlisting\}|\\loc\{([^}]+)\}", document, re.S)
    for entry in entries:
        snippet, source_name = entry.groups()
        if snippet is not None:
            pending.append(snippet)
            continue
        if source_name not in source_cache:
            source = (root / source_name).read_text(encoding="utf-8-sig")
            source_cache[source_name] = {re.sub(r"\s+", "", line.split("//", 1)[0]) for line in source.splitlines()}
        for snippet in pending:
            for line in snippet.splitlines():
                if line.strip():
                    assert re.sub(r"\s+", "", line) in source_cache[source_name], f"{name}: {source_name} differs: {line}"
            excerpt_count += 1
        pending.clear()
    assert not pending, f"{name}: source location missing after excerpt"
assert excerpt_count > 0
print(f"Code excerpts: {excerpt_count} verified against their cited source files")
