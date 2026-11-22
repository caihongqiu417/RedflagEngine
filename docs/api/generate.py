"""Generate the offline Redflag API reference from the checked-out source tree."""
import json
from html import unescape
from pathlib import Path
import re
from tree_sitter import Language, Parser
import tree_sitter_cpp
import tree_sitter_c_sharp

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
OUTPUT = ROOT / "output/api/index.html"
CPP = Parser(Language(tree_sitter_cpp.language()))
CS = Parser(Language(tree_sitter_c_sharp.language()))


def decode(data):
    for encoding in ("utf-8-sig", "gb18030"):
        try:
            return data.decode(encoding)
        except UnicodeDecodeError:
            pass
    return data.decode("utf-8", errors="replace")


def plain(node):
    return node.text.decode("utf-8", errors="replace") if node else ""


def compact(text):
    text = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)
    return " ".join(text.strip().rstrip(";").split())


def docs_before(lines, index):
    docs = []
    while index and lines[index - 1].lstrip().startswith("///"):
        index -= 1
        docs.append(lines[index].lstrip()[3:].strip())
    xml = " ".join(reversed(docs))
    descriptions = []
    for tag, name, detail in re.findall(r'<(summary|param|returns)(?:\s+name="([^"]+)")?>(.*?)</\1>', xml):
        text = unescape(re.sub(r'<paramref name="([^"]+)"\s*/>', r'\1', detail)).strip()
        if tag == "param":
            text = f"{name}：{text}"
        elif tag == "returns":
            text = "返回值：" + text
        descriptions.append(text)
    return " ".join(descriptions)


def prefix(node):
    end = node.end_byte
    for child in node.named_children:
        if child.type in ("compound_statement", "field_initializer_list", "block", "arrow_expression_clause", "accessor_list"):
            end = child.start_byte
            break
    return compact(node.text[:end - node.start_byte].decode("utf-8"))


def group_for(path):
    parts = path.split("/")
    if parts[0] == "RedflagForge":
        return "编辑器"
    if parts[0] == "Redflag-ScriptCore":
        return "C# 脚本"
    if "Platform" in parts:
        return parts[parts.index("Platform") + 1]
    if "RHI" in parts:
        return "RHI"
    if "Panel" in parts:
        return "编辑器"
    return {"Assets":"资源", "Scene":"场景", "Renderer":"渲染", "Events":"事件", "Debug":"调试",
            "ParticleSystem":"粒子", "Scripting":"脚本运行时", "Core":"核心", "Math":"数学"}.get(parts[3] if len(parts) > 3 else "", "核心")


def declarator_name(node):
    while node:
        if node.type in ("field_identifier", "identifier", "type_identifier", "destructor_name", "operator_name"):
            return plain(node)
        if node.type == "operator_cast":
            return "operator " + plain(node.child_by_field_name("type"))
        node = next_declarator(node)
    return ""

def next_declarator(node):
    child = node.child_by_field_name("declarator") or node.child_by_field_name("name")
    if child is None and node.type in ("reference_declarator","parenthesized_declarator"):
        return next(iter(node.named_children), None)
    return child


def cpp_entries(source, path):
    # 导出标记不影响签名。事件声明宏由项目的 Event.h 提供，保留原调用的行号。
    source = re.sub(r"\bREDFLAG_API\b", " ", source)
    source = re.sub(r"(?m)^([ \t]*)EVENT_CLASS_TYPE\([^\n)]*\)", r"\1static EventType GetStaticType(); "
        r"EventType GetEventType() const override; const char* GetName() const override;", source)
    source = re.sub(r"(?m)^([ \t]*)EVENT_CLASS_CATEGORY\([^\n)]*\)", r"\1int GetCategoryFlags() const override;", source)
    root = CPP.parse(source.encode()).root_node
    lines = source.splitlines()
    rows = []

    def add(owner, name, kind, signature, node):
        if name:
            rows.append([owner, name, kind, signature, node.start_point.row + 1, docs_before(lines, node.start_point.row)])

    def visit(node, owner="", template=""):
        kind = node.type
        named = node.child_by_field_name("name")
        name = plain(named)
        if kind == "namespace_definition":
            body = node.child_by_field_name("body")
            if body:
                for child in body.named_children:
                    visit(child, "::".join(filter(None, [owner, name])))
        elif kind.startswith("preproc_if") or kind == "preproc_else":
            for child in node.named_children:
                visit(child, owner)
        elif kind == "template_declaration":
            for child in node.named_children:
                if child.type != "template_parameter_list":
                    visit(child, owner, "template" + plain(node.child_by_field_name("parameters")) + " ")
        elif kind in ("class_specifier", "struct_specifier", "enum_specifier"):
            body = node.child_by_field_name("body")
            if not body or not name:
                return
            label = {"class_specifier":"类", "struct_specifier":"结构体", "enum_specifier":"枚举"}[kind]
            signature = template + node.text[:body.start_byte-node.start_byte].decode("utf-8")
            add(owner, name, label, compact(signature), node)
            public = kind != "class_specifier"
            for member in body.named_children:
                if member.type == "access_specifier":
                    public = plain(member) == "public"
                elif public:
                    visit(member, "::".join(filter(None,[owner,name])))
        elif kind == "enumerator":
            add(owner, name, "枚举值", compact(plain(node)), node)
        elif kind == "preproc_def" and name.startswith(("RF_KEY_", "RF_MOUSE_")):
            add(owner, name, "常量", compact(plain(node)), node)
        elif kind == "alias_declaration":
            add(owner, name, "类型别名", compact(template+plain(node)), node)
        elif kind in ("function_definition", "declaration", "field_declaration"):
            subtype = node.child_by_field_name("type")
            if subtype and subtype.type in ("class_specifier","struct_specifier","enum_specifier"):
                visit(subtype, owner, template)
                return
            sig = template + prefix(node)
            for decl in node.children_by_field_name("declarator"):
                method = any("function_declarator" == leaf.type or leaf.type == "operator_cast" for leaf in declaration_chain(decl))
                add(owner, declarator_name(decl), "方法" if method else "字段", sig, node)
    for child in root.named_children:
        visit(child)
    return rows


def declaration_chain(node):
    while node:
        yield node
        node = next_declarator(node)


def cs_entries(source):
    root = CS.parse(source.encode()).root_node
    lines = source.splitlines()
    rows = []

    def visit(node, owner=""):
        kind = node.type
        name = plain(node.child_by_field_name("name"))
        body = node.child_by_field_name("body")
        if kind == "namespace_declaration":
            for child in body.named_children:
                visit(child, name)
            return
        modifiers = [plain(ch) for ch in node.named_children if ch.type == "modifier"]
        if kind != "enum_member_declaration" and "public" not in modifiers:
            return
        types = {"class_declaration":"类","struct_declaration":"结构体","enum_declaration":"枚举"}
        line = node.start_point.row + 1
        def add(n, label, sig):
            rows.append([owner, n, label, sig, line, docs_before(lines, line-1)])
        if kind in types:
            add(name, types[kind], compact(node.text[:body.start_byte-node.start_byte].decode()))
            for member in body.named_children:
                visit(member, ".".join(filter(None,[owner,name])))
        elif kind == "enum_member_declaration":
            add(name, "枚举值", compact(plain(node)))
        elif kind == "field_declaration":
            declaration = next(ch for ch in node.named_children if ch.type == "variable_declaration")
            for var in declaration.named_children:
                if var.type == "variable_declarator":
                    add(plain(var.child_by_field_name("name")), "字段", prefix(node))
        elif kind == "property_declaration":
            access = node.child_by_field_name("accessors")
            accessors = [plain(ch.child_by_field_name("name")) for ch in access.named_children] if access else ["get"]
            add(name, "属性", prefix(node) + " { " + "; ".join(accessors) + "; }")
        elif kind in ("method_declaration", "constructor_declaration","operator_declaration"):
            sig = prefix(node)
            if kind == "operator_declaration":
                name = re.search(r"operator\s*(\S+?)\s*\(", sig)[1]
                name = "operator " + name
            add(name, "方法", sig)
    for child in root.named_children:
        visit(child)
    return rows


def main():
    sources = {}
    entries = []
    files = sorted(path for directory in ("Redflag/src","RedflagForge/src","Redflag-ScriptCore/Source")
                   for path in (ROOT / directory).rglob("*") if path.suffix in (".h",".hpp",".cs"))
    for file in files:
        path = file.relative_to(ROOT).as_posix()
        source = decode(file.read_bytes())
        rows = cs_entries(source) if path.endswith(".cs") else cpp_entries(source, path)
        sources[path] = source.splitlines()
        for owner, name, kind, signature, line, comment in rows:
            assert 1 <= line <= len(sources[path]) and signature
            entries.append([group_for(path),owner,name,kind,signature,path,line,comment])
    if len(entries) < 500:
        raise RuntimeError("Incomplete API extraction")
    template = (HERE/"reference.html").read_text(encoding="utf-8")
    for path, function in re.findall(r'data-file="([^"]+)"\s+data-function="([^"]+)"', template):
        sources[path] = decode((ROOT / path).read_bytes()).splitlines()
        if not any(function in line for line in sources[path]):
            raise ValueError(f"Guide source link cannot find {function} in {path}")
    data = json.dumps(dict(entries=entries,sources=sources),ensure_ascii=False,separators=(",",":")).replace("<",r"\u003c")
    OUTPUT.parent.mkdir(parents=True,exist_ok=True)
    OUTPUT.write_text(template.replace("__REFERENCE_DATA__",data),encoding="utf-8")
    print(f"{OUTPUT}: {len(entries)} declarations; {len(files)} headers and C# files; {len(sources)} offline sources")


if __name__ == "__main__":
    main()
