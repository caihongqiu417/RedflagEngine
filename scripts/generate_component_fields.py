"""从场景字段表生成 C++ 成员访问器。在根目录运行，加 --check 可检查结果是否过期。"""
import argparse
import json
from pathlib import Path


SCENE = Path(__file__).resolve().parents[1] / "Redflag/src/Redflag/Scene"
KINDS = {"float": "Float", "vec3": "Vector3", "enum": "Enum"}


def cpp(text):
    return json.dumps(text, ensure_ascii=False)


def generate(components):
    lines = ["// 由 scripts/generate_component_fields.py 生成。修改 ComponentFields.json 后重新运行。"]
    registrations = []
    for component in components:
        name, fields = component["name"], component["fields"]
        checks, bindings = [], []
        for field in fields:
            member = field.get("member", field["name"])
            kind = field["type"]
            choices = field.get("choices", [])
            arguments = ", ".join(cpp(field.get(key, "")) for key in ("name", "label", "unit", "help", "constraint"))
            choices_cpp = ", ".join(map(cpp, choices))
            enumerators = ", ".join(f"decltype({name}::{member})::{choice}" for choice in choices)
            if kind == "enum":
                checks.append("(" + " || ".join(f"c.{member} == decltype(c.{member})::{choice}" for choice in choices) + ")")
            else:
                expected = "float" if kind == "float" else "glm::vec3"
                lines.append(f"static_assert(std::is_same_v<decltype({name}::{member}), {expected}>);")
                checks.append(f"Finite(c.{member})")
                if field.get("positive"):
                    checks.append(f"Positive(c.{member})")
            bindings.append(f"Bind({{{arguments}, FieldKind::{KINDS[kind]}, {{{choices_cpp}}}}}, "
                            f"&{name}::{member}, Valid, {{{enumerators}}})")
        if rule := component.get("diameter"):
            checks.append(f"c.{rule['height']} >= 2.f * c.{rule['radius']}")
        lines.extend([f"bool Valid(const {name}& c)", "{", "\treturn " + " &&\n\t\t".join(checks) + ";", "}", ""])
        entries = ",\n\t\t\t".join(bindings)
        registrations.append(f"{{{cpp(name)}, {cpp(component['label'])}, {{\n\t\t\t{entries}\n\t\t}}, "
                             f"[](Entity e) {{ return e && e.HasComponent<{name}>() && Valid(e.GetComponent<{name}>()); }}}}")
    return "\n".join(lines) + "std::vector<Component> RegisterComponents()\n{\n\treturn {\n\t\t" + ",\n\t\t".join(registrations) + "\n\t};\n}\n"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    options = parser.parse_args()
    result = generate(json.loads((SCENE / "ComponentFields.json").read_text("utf8"))["components"])
    target = SCENE / "ComponentFields.inc"
    if options.check:
        if not target.exists() or target.read_text("utf8") != result:
            raise SystemExit("ComponentFields.inc 需要更新。请运行 python scripts/generate_component_fields.py。")
    else:
        target.write_text(result, encoding="utf8", newline="\n")
