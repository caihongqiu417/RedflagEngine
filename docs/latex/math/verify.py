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

root = Path(__file__).resolve().parents[3]
sources = [root / "RedflagForge/assets/shaders" / name for name in
           ["Renderer3D_Mesh.glsl", "SSAO.glsl", "RSMIndirect.glsl", "RSMDepth.glsl"]]
sources.append(root / "Redflag/src/Redflag/Renderer/Renderer3D.cpp")
statements = set()
for source in sources:
    for line in source.read_text(encoding="utf-8").splitlines():
        statements.add(re.sub(r"\s+", "", line.split("//", 1)[0]))
document = "\n".join(Path(__file__).with_name(name).read_text(encoding="utf-8")
                     for name in ["main.tex", "rsm.tex"])
for snippet in re.findall(r"\\begin\{lstlisting\}(?!\[)(.*?)\\end\{lstlisting\}", document, re.S):
    for line in snippet.splitlines():
        if line.strip():
            assert re.sub(r"\s+", "", line) in statements, f"Snippet differs from source: {line}"
print("Code excerpts: every statement matches current project source (excluding comments/spacing)")
