"""Build a six-part lesson per topic and publish one locally navigable PDF."""
import csv
import json
import shutil
import subprocess
from pathlib import Path

folder = Path(__file__).resolve().parent
root = folder.parents[2]
topics = json.loads((folder / "topics.json").read_text(encoding="utf8"))
stages = ["前置知识", "公式推导", "公式", "逐行伪代码", "接口与数据结构", "验证标准"]


def generate():
    sections = []
    gi = json.loads((root / "output/profiling/gi/metrics.json").read_text(encoding="utf8"))
    screen = json.loads((root / "output/screen-effects/metrics.json").read_text(encoding="utf8"))
    values = dict(screen)
    mirror = json.loads((root / "output/profiling/scene-reflections/frames.json").read_text(encoding="utf8"))
    values["mirror-stripe-ratio"] = round(mirror["stripe_ratio"], 3)
    for sample in ("white", "gray", "black", "board"):
        case, stripe = ("mirror-blocked", "white") if sample == "board" else ("mirror-scene", sample)
        rgb = mirror["cases"][case]["stripes"][stripe]["rgb"]
        values["mirror-" + sample + "-r"] = round(rgb[0], 3)
        values["mirror-" + sample + "-g"] = round(rgb[1], 3)
    with (root / "output/profiling/rough-reflections/test-quality.csv").open(newline="") as file:
        for row in csv.DictReader(file):
            if row["mode"] in ("raw", "filtered", "reference"):
                for field in ("mean_rgb_mse", "adjacent_rgb_mse", "bright_minus_middle_red"):
                    values["rough-" + row["mode"] + "-" + field] = float(row[field])
    values["reference-mean"] = gi["reference_mean_rgb"]["indirect"]
    for row in gi["results"]:
        for field in ["mse", "mean_rgb", "reused_fraction_visible", "reused_fraction_positive"]:
            value = row[field]
            values[row["method"] + "-" + field] = value * (100 if "fraction" in field else 1)
    for key, value in values.items():
        if isinstance(value, (int, float)):
            sections.append(r"\expandafter\def\csname metric-" + key + r"\endcsname{" + f"{value:.5g}" + "}")
    sections.append(r"\newcommand{\measured}[1]{\csname metric-#1\endcsname}")
    index = ["# 教材、实现与实测", "", "从下列章节可分别打开可编辑笔记、项目源码和原作者材料。", ""]
    for topic in topics:
        key = topic["id"]
        source = folder / (key + ".tex")
        lesson = source.read_text(encoding="utf8")
        positions = [lesson.index("% " + title + "\n") for title in stages]
        if positions != sorted(positions):
            raise ValueError(f"{source.name}: lesson sections out of order")
        for path in topic["code"]:
            if not (root / path).is_file():
                raise FileNotFoundError(path)
        parts = [r"\clearpage", r"\section{" + topic["title"] + "}",
                 r"\label{lesson-" + key + "}", r"\input{" + key + "}"]
        links = [r"\href{" + url.replace("%", r"\%") + "}{" + label + "}" for label, url in topic["materials"]]
        links += [r"\href{run:../../docs/latex/lighting/" + key + ".tex}{可编辑笔记}"]
        index += ["## " + topic["title"], "", "[本章笔记](" + key + ".tex) · " + " · ".join(
            "[" + path.split("/")[-1] + "](../../../" + path + ")" for path in topic["code"]), "",
            " · ".join("[" + label + "](" + url + ")" for label, url in topic["materials"]), ""]
        parts += [r"\par\noindent{\small\color{quiet}材料索引\enspace " + r"\enspace · \enspace ".join(links) + "}"]
        if key == "sampling":
            parts.append(r"\input{point-materials}")
            index += ["[点光源下的金属高光](point-materials.tex)", ""]
        if key == "ddgi":
            parts.extend([r"\input{probe-feedback}", r"\input{sky-probes}", r"\input{probe-baking}"])
            index += ["[墙面之间的多次漫反射](probe-feedback.tex) · [窗外的环境光](sky-probes.tex) · [保存和载入烘焙探针](probe-baking.tex)", ""]
        if key == "ssr":
            parts.extend([r"\input{mirror}", r"\input{rough-reflections}"])
            index += ["[屏幕外的倒影与遮挡](mirror.tex) · [PBRT 镜面反射](https://pbr-book.org/4ed/Reflection_Models/Specular_Reflection_and_Transmission) · [AMD 混合反射](https://gpuopen.com/manuals/fidelityfx_sdk/samples/hybrid-reflections/)", ""]
            index += ["[粗糙倒影的降噪](rough-reflections.tex) · [反射滤波着色器](../../../Redflag/src/Redflag/Renderer/ScreenSpace/Shaders/ReflectionFilter.comp) · [HPG 2010 作者论文](https://jo.dreggn.org/home/2010_atrous.pdf) · [AMD 反射降噪说明](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/denoiser/)", ""]
        sections.extend(parts)
    index += ["## 方向光与投影", "",
              "[Stanford Bunny 的同机位截帧](directional.tex) · [PBRT 第四版 §12.3](https://pbr-book.org/4ed/Light_Sources/Distant_Lights)", "",
              "## 光谱、色散、球谐函数与体素", "",
              "[专题与配图](light-representations.tex) · [棱镜插图的原件和许可](figures/README.md)", "",
              "[PBRT 第四版，光谱分布](https://pbr-book.org/4ed/Radiometry,_Spectra,_and_Color/Representing_Spectral_Distributions) · "
              "[PBRT 第四版，玻璃色散](https://pbr-book.org/4ed/Reflection_Models/Specular_Reflection_and_Transmission) · "
              "[漫反射环境光的球谐近似](https://graphics.stanford.edu/papers/envmap/) · "
              "[三角网格的体素化](https://michael-schwarz.com/research/publ/2010/vox/)", "",
              "## 镜头外的灯牌", "",
              "[光照积分、探针实拍与读数](emissive-panel.tex) · [PBRT 第四版 §12.4](https://pbr-book.org/4ed/Light_Sources/Area_Lights)", "",
              "## 光线与场景中的三角形", "",
              "[包围盒与空走廊示意图](triangle-groups.tex) · [PBRT 第四版 §7.3](https://pbr-book.org/4ed/Primitives_and_Intersection_Acceleration/Bounding_Volume_Hierarchies)", ""]
    (folder / "chapters.generated.tex").write_text("\n".join(sections) + "\n", encoding="utf8")
    (folder / "README.md").write_text("\n".join(index) + "\n", encoding="utf8")


if __name__ == "__main__":
    generate()
    build = folder / "build"
    build.mkdir(exist_ok=True)
    for _ in range(2):
        subprocess.run(["xelatex", "-halt-on-error", "-interaction=batchmode", "-output-directory=build", "main.tex"],
                       cwd=folder, check=True)
    target = root / "output/pdf/Redflag-Realtime-Lighting.pdf"
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(build / "main.pdf", target)
    print(target)
