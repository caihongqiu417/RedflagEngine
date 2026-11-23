#pragma once

#include "Redflag/Scene/ComponentReflection.h"
#include <charconv>
#include <iomanip>

// MCP 在没有运行引擎时也能说明省略字段的默认值。这份 JSON 直接取组件构造后的值。
inline std::string ComponentDefaults()
{
    using namespace Redflag::Reflection;
    std::ostringstream out;
    auto number = [&](float value) {
        char text[40];
        auto result = std::to_chars(text, text + sizeof(text), value);
        out.write(text, result.ptr - text);
    };
    out << "{\n";
    bool first = true;
    for (const Component& type : Types()) {
        if (!first) out << ",\n";
        first = false;
        out << "  " << std::quoted(type.Name) << ": {";
        for (size_t i = 0; i < type.Fields.size(); ++i) {
            const Field& field = type.Fields[i];
            out << (i ? ", " : "") << std::quoted(field.Name) << ": ";
            if (const float* scalar = std::get_if<float>(&field.Default)) number(*scalar);
            else if (const auto* v = std::get_if<glm::vec3>(&field.Default)) {
                out << "["; number(v->x); out << ", "; number(v->y); out << ", "; number(v->z); out << "]";
            } else out << std::quoted(std::get<std::string>(field.Default));
        }
        out << "}";
    }
    return out.str() + "\n}\n";
}

inline void CheckComponentReflection()
{
    using namespace Redflag::Reflection;
    Ref<Scene> scene = CreateRef<Scene>();
    Entity actor = scene->CreateEntity("Player");
    Entity crate = scene->CreateEntity("Crate");
    auto& capsule = actor.AddComponent<CapsuleCollider3DComponent>();
    crate.AddComponent<BoxCollider3DComponent>();
    crate.AddComponent<RigidBody3DComponent>();
    const Component* reflected = FindComponent("CapsuleCollider3DComponent");
    Check(reflected && !FindComponent("Missing") && !reflected->FindField("Missing"), "look up stable component and field names");
    const Field& radius = *reflected->FindField("Radius");
    const Field& axis = *reflected->FindField("Axis");
    Check(radius.Read(actor) == std::optional<Value>(capsule.Radius) && radius.Kind == FieldKind::Float &&
        !radius.Unit.empty() && !radius.Help.empty(), "read the actual component and its field description");
    Check(radius.Write(actor,.45f) && capsule.Radius == .45f &&
        !radius.Write(actor,1.5f) && !radius.Write(actor,std::numeric_limits<float>::quiet_NaN()) &&
        !radius.Write(actor,std::string("0.45")) && capsule.Radius == .45f, "invalid types and dimensions preserve the previous capsule");
    Check(axis.Write(actor,std::string("Z")) && capsule.Axis == CapsuleAxis::Z && !axis.Write(actor,std::string("sideways")),
        "write enums by their scene names");
    Check(!radius.Read(crate) && !radius.Write(crate,.3f), "a missing component cannot be read or added by a field edit");
    const Field& half = *FindComponent("BoxCollider3DComponent")->FindField("HalfSize");
    Check(half.Write(crate,glm::vec3(2,1,.5f)) && !half.Write(crate,glm::vec3(2,0,.5f)) &&
        crate.GetComponent<BoxCollider3DComponent>().HalfSize == glm::vec3(2,1,.5f), "validate all three box half sizes before writing");
    const Field& bodyType = *FindComponent("RigidBody3DComponent")->FindField("BodyType");
    Check(bodyType.Write(crate,std::string("Kinematic")) && crate.GetComponent<RigidBody3DComponent>().Type == BodyType3D::Kinematic,
        "scene field BodyType reaches the C++ Type member");

    const char* path = "../bin-int/Tests/reflected-fields.redflag";
    SceneSerializer(scene).Serialize(path);
    Ref<Scene> restored = CreateRef<Scene>();
    Check(SceneSerializer(restored).Deserialize(path) &&
        radius.Read(restored->GetEntityByUUID(actor.GetUUID())) == radius.Read(actor) &&
        half.Read(restored->GetEntityByUUID(crate.GetUUID())) == half.Read(crate), "reflected writes reopen through the regular scene serializer");
    std::ofstream(path) << "Scene: BadEnum\nEntities:\n  - Entity: 42\n    RigidBody3DComponent: {BodyType: Floating, Density: 1}\n";
    Check(!SceneSerializer(restored).Deserialize(path), "invalid body type is also rejected when opening YAML");
    std::ofstream(path) << "Scene: BadCentre\nEntities:\n  - Entity: 42\n    BoxCollider3DComponent: {Center: [0, 1], HalfSize: [1, 1, 1]}\n";
    restored = CreateRef<Scene>();
    Check(!SceneSerializer(restored).Deserialize(path), "a provided box centre needs three coordinates");
    std::ofstream(path) << "Scene: OldCapsule\nEntities:\n  - Entity: 42\n    CapsuleCollider3DComponent: {Radius: 0.3, Height: 1.7}\n";
    restored = CreateRef<Scene>();
    const Field& center = *reflected->FindField("Center");
    Check(SceneSerializer(restored).Deserialize(path) && axis.Read(restored->GetEntityByUUID(42)) == axis.Default &&
        center.Read(restored->GetEntityByUUID(42)) == center.Default,
        "older scenes can omit the capsule axis and centre");
    std::ifstream defaults("../output/api/component-defaults.json");
    const std::string exported{std::istreambuf_iterator<char>(defaults), std::istreambuf_iterator<char>()};
    Check(exported == ComponentDefaults(), "MCP default metadata matches the compiled constructors");
}
