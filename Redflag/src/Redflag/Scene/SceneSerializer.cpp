#include"rfpch.h"

#include"SceneSerializer.h"
#include <yaml-cpp/yaml.h>

#include"Entity.h"
#include"Components.h"
#include "Redflag/Assets/AssetManager.h"
#include "Redflag/Renderer/Model.h"
#include "Redflag/Utils/FileSystem.h"

#include <fstream>
namespace YAML {
    template<>
    struct convert<glm::vec2>
    {
        static Node encode(const glm::vec2& rhs)
        {
            Node node;
            node.push_back(rhs.x);
            node.push_back(rhs.y);
            return node;
        }

        static bool decode(const Node& node, glm::vec2& rhs)
        {
            if (!node.IsSequence() || node.size() != 2)
                return false;
            rhs.x = node[0].as<float>();
            rhs.y = node[1].as<float>();
            return true;
        }
    };

    template<>
    struct convert<glm::vec3>
    {
        static Node encode(const glm::vec3& rhs)
        {
            Node node;
            node.push_back(rhs.x);
            node.push_back(rhs.y);
            node.push_back(rhs.z);
            return node;
        }

        static bool decode(const Node& node, glm::vec3& rhs)
        {
            if (!node.IsSequence() || node.size() != 3)
                return false;
            rhs.x = node[0].as<float>();
            rhs.y = node[1].as<float>();
            rhs.z = node[2].as<float>();
            return true;
        }
    };

    template<>
    struct convert<glm::vec4>
    {
        static Node encode(const glm::vec4& rhs)
        {
            Node node;
            node.push_back(rhs.x);
            node.push_back(rhs.y);
            node.push_back(rhs.z);
            node.push_back(rhs.w);
            return node;
        }

        static bool decode(const Node& node, glm::vec4& rhs)
        {
            if (!node.IsSequence() || node.size() != 4)
                return false;
            rhs.x = node[0].as<float>();
            rhs.y = node[1].as<float>();
            rhs.z = node[2].as<float>();
            rhs.w = node[3].as<float>();
            return true;
        }
    };

} // namespace YAML



namespace Redflag {
	SceneSerializer::SceneSerializer(const Ref<Scene>& scene)
		:m_Scene(scene)
	{

	}

    YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec2& v)
    {
        out << YAML::Flow;
        out << YAML::BeginSeq << v.x << v.y << YAML::EndSeq;
        return out;
    }

	YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec3& v)
	{
		out << YAML::Flow;
		out << YAML::BeginSeq << v.x << v.y << v.z << YAML::EndSeq;
		return out;
	}

	YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec4& v)
	{
		out << YAML::Flow;
		out << YAML::BeginSeq << v.x << v.y << v.z <<v.w<< YAML::EndSeq;
		return out;
	}

    template<typename BodyType>
    static const char* BodyTypeName(BodyType type)
    {
        if (type == BodyType::Dynamic) return "Dynamic";
        if (type == BodyType::Kinematic) return "Kinematic";
        return "Static";
    }

    template<typename BodyType>
    static BodyType ReadBodyType(const std::string& name)
    {
        if (name == "Dynamic") return BodyType::Dynamic;
        if (name == "Kinematic") return BodyType::Kinematic;
        return BodyType::Static;
    }

    static const char* ScriptFieldTypeToString(ScriptFieldType type)
    {
        switch (type)
        {
            case ScriptFieldType::Float:   return "Float";
            case ScriptFieldType::Int:     return "Int";
            case ScriptFieldType::Bool:    return "Bool";
            case ScriptFieldType::Vector3: return "Vector3";
            case ScriptFieldType::Entity:  return "Entity";
            default:                       return "None";
        }
    }

    static ScriptFieldType ScriptFieldTypeFromString(const std::string& typeString)
    {
        if (typeString == "Float")   return ScriptFieldType::Float;
        if (typeString == "Int")     return ScriptFieldType::Int;
        if (typeString == "Bool")    return ScriptFieldType::Bool;
        if (typeString == "Vector3") return ScriptFieldType::Vector3;
        if (typeString == "Entity")  return ScriptFieldType::Entity;
        return ScriptFieldType::None;
    }

    template<typename T>
    static bool ReadScriptFieldBytes(const std::vector<uint8_t>& bytes, T& outValue)
    {
        if (bytes.size() != sizeof(T))
            return false;

        memcpy(&outValue, bytes.data(), sizeof(T));
        return true;
    }

    template<typename T>
    static std::vector<uint8_t> MakeScriptFieldBytes(const T& value)
    {
        std::vector<uint8_t> bytes(sizeof(T));
        memcpy(bytes.data(), &value, sizeof(T));
        return bytes;
    }

    static void SerializeScriptFieldValue(YAML::Emitter& out, ScriptFieldType type, const std::vector<uint8_t>& bytes)
    {
        switch (type)
        {
            case ScriptFieldType::Float:
            {
                float value = 0.0f;
                if (ReadScriptFieldBytes(bytes, value))
                    out << value;
                break;
            }
            case ScriptFieldType::Int:
            {
                int32_t value = 0;
                if (ReadScriptFieldBytes(bytes, value))
                    out << value;
                break;
            }
            case ScriptFieldType::Bool:
            {
                bool value = false;
                if (ReadScriptFieldBytes(bytes, value))
                    out << value;
                break;
            }
            case ScriptFieldType::Vector3:
            {
                glm::vec3 value(0.0f);
                if (ReadScriptFieldBytes(bytes, value))
                    out << value;
                break;
            }
            case ScriptFieldType::Entity:
            {
                uint64_t value = 0;
                if (ReadScriptFieldBytes(bytes, value))
                    out << value;
                break;
            }
            default:
                out << YAML::Null;
                break;
        }
    }

    static bool DeserializeScriptFieldValue(const YAML::Node& valueNode, ScriptFieldType type, std::vector<uint8_t>& outBytes)
    {
        if (!valueNode || type == ScriptFieldType::None)
            return false;

        switch (type)
        {
            case ScriptFieldType::Float:
                outBytes = MakeScriptFieldBytes(valueNode.as<float>());
                return true;
            case ScriptFieldType::Int:
                outBytes = MakeScriptFieldBytes((int32_t)valueNode.as<int>());
                return true;
            case ScriptFieldType::Bool:
                outBytes = MakeScriptFieldBytes(valueNode.as<bool>());
                return true;
            case ScriptFieldType::Vector3:
                outBytes = MakeScriptFieldBytes(valueNode.as<glm::vec3>());
                return true;
            case ScriptFieldType::Entity:
                outBytes = MakeScriptFieldBytes(valueNode.as<uint64_t>());
                return true;
            default:
                return false;
        }
    }

    struct MaterialMap
    {
        const char* Name;
        Ref<Texture2D> Material::*Texture;
    };

    static constexpr MaterialMap s_MaterialMaps[] = {
        { "BaseColorMap", &Material::BaseColorMap }, { "NormalMap", &Material::NormalMap },
        { "MetallicRoughnessMap", &Material::MetallicRoughnessMap }, { "AOMap", &Material::AOMap },
        { "AlphaMap", &Material::AlphaMap }, { "EmissiveMap", &Material::EmissiveMap }
    };

    static void WriteMaterial(YAML::Emitter& out, const Material& material, const Material* original = nullptr)
    {
        out << YAML::Key << "BaseColor" << YAML::Value << material.BaseColor;
        out << YAML::Key << "Metallic" << YAML::Value << material.Metallic;
        out << YAML::Key << "Roughness" << YAML::Value << material.Roughness;
        out << YAML::Key << "AO" << YAML::Value << material.AO;
        if (material.Emission != glm::vec3(0))
            out << YAML::Key << "Emission" << YAML::Value << material.Emission;

        // 模型内的图片没有独立文件名。只记下哪些贴图沿用模型，重开场景时即可找回原图。
        std::vector<std::string> modelMaps;
        for (const MaterialMap& map : s_MaterialMaps)
        {
            const Ref<Texture2D>& texture = material.*map.Texture;
            if (!texture) continue;
            if (original && texture == original->*map.Texture)
                modelMaps.emplace_back(map.Name);
            else if (!texture->GetPath().empty())
                out << YAML::Key << map.Name << YAML::Value << std::filesystem::proximate(texture->GetPath()).generic_u8string();
        }
        if (!modelMaps.empty())
            out << YAML::Key << "ModelMaps" << YAML::Value << YAML::Flow << modelMaps;

        const char* alpha = "Opaque";
        if (material.Alpha == AlphaMode::Mask) alpha = "Mask";
        if (material.Alpha == AlphaMode::Blend) alpha = "Blend";
        out << YAML::Key << "AlphaMode" << YAML::Value << alpha;
        out << YAML::Key << "AlphaCutoff" << YAML::Value << material.AlphaCutoff;
        out << YAML::Key << "DoubleSided" << YAML::Value << material.DoubleSided;
        out << YAML::Key << "RefractionIndex" << YAML::Value << material.RefractionIndex;
        out << YAML::Key << "Thickness" << YAML::Value << material.Thickness;
    }

    static void ReadMaterial(const YAML::Node& node, Material& material, const Material* original = nullptr)
    {
        if (node["BaseColor"]) material.BaseColor = node["BaseColor"].as<glm::vec4>();
        material.Metallic = node["Metallic"].as<float>(material.Metallic);
        material.Roughness = node["Roughness"].as<float>(material.Roughness);
        material.AO = node["AO"].as<float>(material.AO);
        if (node["Emission"]) material.Emission = node["Emission"].as<glm::vec3>();
        for (const MaterialMap& map : s_MaterialMaps)
        {
            if (node[map.Name])
                material.*map.Texture = Texture2D::Create(std::filesystem::u8path(node[map.Name].as<std::string>()).string());
        }
        if (original)
        {
            for (const YAML::Node& entry : node["ModelMaps"])
            {
                const std::string name = entry.as<std::string>();
                for (const MaterialMap& map : s_MaterialMaps)
                    if (name == map.Name) material.*map.Texture = original->*map.Texture;
            }
        }

        const std::string alpha = node["AlphaMode"].as<std::string>("Opaque");
        if (alpha == "Blend") material.Alpha = AlphaMode::Blend;
        else if (alpha == "Mask") material.Alpha = AlphaMode::Mask;
        else if (alpha == "Opaque") material.Alpha = AlphaMode::Opaque;
        else throw std::runtime_error("Unknown material AlphaMode: " + alpha);
        material.AlphaCutoff = node["AlphaCutoff"].as<float>(material.AlphaCutoff);
        material.DoubleSided = node["DoubleSided"].as<bool>(material.DoubleSided);
        material.RefractionIndex = node["RefractionIndex"].as<float>(material.RefractionIndex);
        material.Thickness = node["Thickness"].as<float>(material.Thickness);
    }

    static void ReadModelFile(const YAML::Node& node, AssetID& id, std::string& path)
    {
        path = node["FilePath"].as<std::string>("");
        id = node["ModelID"].as<AssetID>(0);
        if (!id)
            id = AssetManager::Add<Model>(path);
        else if (!AssetManager::GetLibrary().Get(id) && std::filesystem::exists(path + ".rfmeta"))
        {
            // 项目外的模型也能按路径找回，但其 ID 须和场景中保存的一致。
            auto found = AssetManager::Add<Model>(path);
            if (found && found != id) throw std::runtime_error("Model ID does not match: " + path);
        }
        if (const auto* info = AssetManager::GetLibrary().Get(id))
            path = std::filesystem::proximate(info->Path).generic_string();
    }

    static void SerializeEntity(YAML::Emitter& out, Entity entity, const std::filesystem::path& folder)
    {
        RF_CORE_ASSERT(entity.HasComponent<IDComponent>());
        // 开始实体的 Map
        out << YAML::BeginMap;
        out << YAML::Key << "Entity" << YAML::Value << entity.GetUUID();

        if (entity.HasComponent<TagComponent>())
        {
            out << YAML::Key << "TagComponent";
            out << YAML::BeginMap;

            auto& tag = entity.GetComponent<TagComponent>().Tag;
            out << YAML::Key << "Tag" << YAML::Value << tag;

            out << YAML::EndMap; // 结束 TagComponent Map
        }

        if (entity.HasComponent<TransformComponent>())
        {
            out << YAML::Key << "TransformComponent";
            out << YAML::BeginMap;

            auto& tc = entity.GetComponent<TransformComponent>();
            out << YAML::Key << "Translation" << YAML::Value << tc.Translation;
            out << YAML::Key << "Rotation" << YAML::Value << tc.Rotation;
            out << YAML::Key << "Scale" << YAML::Value << tc.Scale;

            out << YAML::EndMap; // 结束 TransformComponent Map
        }

        if (entity.HasComponent<CameraComponent>())
        {
            out << YAML::Key << "CameraComponent";
            out << YAML::BeginMap;

            auto& cameraComponent = entity.GetComponent<CameraComponent>();
            auto& camera = cameraComponent.Camera;

            out << YAML::Key << "Camera" << YAML::Value;
            out << YAML::BeginMap;
            out << YAML::Key << "ProjectionType" << YAML::Value << (int)camera.GetProjectionType();
            out << YAML::Key << "PerspectiveFov" << YAML::Value << camera.GetPerspectiveFOV();
            out << YAML::Key << "PerspectiveNear" << YAML::Value << camera.GetPerspectiveNearClip();
            out << YAML::Key << "PerspectiveFar" << YAML::Value << camera.GetPerspectiveFarClip();
            out << YAML::Key << "OrthographicSize" << YAML::Value << camera.GetOrthographicSize();
            out << YAML::Key << "OrthographicNear" << YAML::Value << camera.GetOrthographicNearClip();
            out << YAML::Key << "OrthographicFar" << YAML::Value << camera.GetPerspectiveFarClip();
            out << YAML::EndMap; // 结束 Camera Map

            out << YAML::Key << "Primary" << YAML::Value << cameraComponent.Primary;
            out << YAML::Key << "FixedAspectRatio" << YAML::Value << cameraComponent.FixedAspectRatio;

            out << YAML::EndMap; // 结束 CameraComponent Map
        }

        if (entity.HasComponent<ScriptComponent>())
        {
            auto& scriptComponent = entity.GetComponent<ScriptComponent>();
            out << YAML::Key << "ScriptComponent";  
            out << YAML::BeginMap;
            out << YAML::Key << "Class" << YAML::Value << scriptComponent.ClassName;  

            auto& fieldCache = ScriptEngine::GetScriptFieldCache(entity.GetUUID());
            auto scriptClasses = ScriptEngine::GetEntityClasses();
            auto scriptClassIt = scriptClasses.find(scriptComponent.ClassName);
            if (scriptClassIt != scriptClasses.end() && !fieldCache.empty())
            {
                out << YAML::Key << "ScriptFields" << YAML::Value << YAML::BeginSeq;
                for (const auto& [fieldName, bytes] : fieldCache)
                {
                    const ScriptField* field = scriptClassIt->second->GetField(fieldName);
                    if (!field || bytes.size() != ScriptFieldTypeSize(field->Type))
                        continue;

                    out << YAML::BeginMap;
                    out << YAML::Key << "Name" << YAML::Value << fieldName;
                    out << YAML::Key << "Type" << YAML::Value << ScriptFieldTypeToString(field->Type);
                    out << YAML::Key << "Value" << YAML::Value;
                    SerializeScriptFieldValue(out, field->Type, bytes);
                    out << YAML::EndMap;
                }
                out << YAML::EndSeq;
            }

            out << YAML::EndMap;
        }

        if (entity.HasComponent<SpriteRendererComponent>())
        {
            out << YAML::Key << "SpriteRendererComponent";
            out << YAML::BeginMap;

            auto& spriteRendererComponent = entity.GetComponent<SpriteRendererComponent>();
            out << YAML::Key << "Color" << YAML::Value << spriteRendererComponent.Color;
            out << YAML::Key << "TilingFactor" << YAML::Value << spriteRendererComponent.TilingFactor;
            if (spriteRendererComponent.Texture)
                out << YAML::Key << "TexturePath" << YAML::Value << spriteRendererComponent.Texture->GetPath();

            out << YAML::EndMap; // SpriteRendererComponent Map
        }

        if (entity.HasComponent<CircleRendererComponent>())
        {
            out << YAML::Key << "CircleRendererComponent";
            out << YAML::BeginMap;

            auto& crc = entity.GetComponent<CircleRendererComponent>();
            out << YAML::Key << "Color"     << YAML::Value << crc.Color;
            out << YAML::Key << "Thickness" << YAML::Value << crc.Thickness;
            out << YAML::Key << "Fade"      << YAML::Value << crc.Fade;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<MeshRendererComponent>())
        {
            out << YAML::Key << "MeshRendererComponent";
            out << YAML::BeginMap;

            auto& mr = entity.GetComponent<MeshRendererComponent>();
            out << YAML::Key << "Color" << YAML::Value << mr.Color;
            // MeshData 目前只支持立方体，后期扩展文件路径
            out << YAML::Key << "MeshType" << YAML::Value << "Cube";

            // PBR 材质参数（BaseColor 取 mat->BaseColor，它才是 Inspector 编辑的值）
            if (mr.Mat)
            {
                WriteMaterial(out, *mr.Mat);
            }

            out << YAML::EndMap;
        }

        if (entity.HasComponent<ModelRendererComponent>())
        {
            out << YAML::Key << "ModelRendererComponent";
            out << YAML::BeginMap;

            auto& mr = entity.GetComponent<ModelRendererComponent>();
            out << YAML::Key << "ModelID" << YAML::Value << mr.ModelID;
            out << YAML::Key << "FilePath" << YAML::Value << mr.FilePath;
            out << YAML::Key << "Color"    << YAML::Value << mr.Color;
            if (!mr.LODs.empty())
            {
                out << YAML::Key << "LODs" << YAML::Value << YAML::BeginSeq;
                for (const auto& lod : mr.LODs)
                    out << YAML::BeginMap << YAML::Key << "ModelID" << YAML::Value << lod.ModelID
                        << YAML::Key << "FilePath" << YAML::Value << lod.FilePath
                        << YAML::Key << "ScreenHeight" << YAML::Value << lod.ScreenHeight << YAML::EndMap;
                out << YAML::EndSeq;
            }

            // per-submesh 覆盖材质 PBR 参数
            if (!mr.OverrideMaterials.empty())
            {
                out << YAML::Key << "OverrideMaterials";
                out << YAML::BeginSeq;
                for (size_t i = 0; i < mr.OverrideMaterials.size(); i++)
                {
                    out << YAML::BeginMap;
                    if (mr.OverrideMaterials[i])
                    {
                        const Material* original = mr.ModelAsset && i < mr.ModelAsset->GetSubMeshes().size()
                            ? mr.ModelAsset->GetSubMeshes()[i].Mat.get() : nullptr;
                        WriteMaterial(out, *mr.OverrideMaterials[i], original);
                    }
                    out << YAML::EndMap;
                }
                out << YAML::EndSeq;
            }

            out << YAML::EndMap;
        }

        if (entity.HasComponent<RigidBody2DComponent>())
        {
            out << YAML::Key << "RigidBodyComponent";
            out << YAML::BeginMap;

            auto& rb2dComponent = entity.GetComponent<RigidBody2DComponent>();
            out << YAML::Key << "Body Type" << YAML::Value << BodyTypeName(rb2dComponent.Type);
            out << YAML::Key << "Fixed Rotation" << YAML::Value << rb2dComponent.FixedRotation;

            out << YAML::EndMap; 
        }

        if (entity.HasComponent<PickupComponent>())
            out << YAML::Key << "PickupComponent" << YAML::Value << YAML::BeginMap << YAML::EndMap;

        if (entity.HasComponent<SkeletonAnimationComponent>())
        {
            const SkeletonAnimationComponent& player = entity.GetComponent<SkeletonAnimationComponent>();
            const auto relative = player.File.lexically_relative(folder);
            out << YAML::Key << "SkeletonAnimationComponent" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "File" << YAML::Value << (relative.empty() ? player.File : relative).generic_u8string();
            out << YAML::Key << "Playing" << YAML::Value << player.Playing;
            out << YAML::Key << "Time" << YAML::Value << player.Time;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<RigidBody3DComponent>())
        {
            const RigidBody3DComponent& body = entity.GetComponent<RigidBody3DComponent>();
            out << YAML::Key << "RigidBody3DComponent" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "BodyType" << YAML::Value << BodyTypeName(body.Type);
            out << YAML::Key << "Density" << YAML::Value << body.Density;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<BoxCollider3DComponent>())
        {
            const BoxCollider3DComponent& box = entity.GetComponent<BoxCollider3DComponent>();
            out << YAML::Key << "BoxCollider3DComponent" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "HalfSize" << YAML::Value << box.HalfSize;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<BoxCollider2DComponent>())
        {
            out << YAML::Key << "BoxCollider2DComponent";
            out << YAML::BeginMap;

            auto& bc2dComponent = entity.GetComponent<BoxCollider2DComponent>();
            out << YAML::Key << "Offset" << YAML::Value << bc2dComponent.Offset;
            out << YAML::Key << "Size" << YAML::Value << bc2dComponent.Size;
            out << YAML::Key << "Density" << YAML::Value << bc2dComponent.Density;
            out << YAML::Key << "Friction" << YAML::Value << bc2dComponent.Friction;
            out << YAML::Key << "Restitution" << YAML::Value << bc2dComponent.Restitution;
            out << YAML::Key << "RestitutionThreshold" << YAML::Value << bc2dComponent.RestitutionThreshold;

            out << YAML::EndMap; 
        }

        if (entity.HasComponent<CircleCollider2DComponent>())
        {
            out << YAML::Key << "CircleCollider2DComponent";
            out << YAML::BeginMap;

            auto& cc2d = entity.GetComponent<CircleCollider2DComponent>();
            out << YAML::Key << "Offset" << YAML::Value << cc2d.Offset;
            out << YAML::Key << "Radius" << YAML::Value << cc2d.Radius;
            out << YAML::Key << "Density" << YAML::Value << cc2d.Density;
            out << YAML::Key << "Friction" << YAML::Value << cc2d.Friction;
            out << YAML::Key << "Restitution" << YAML::Value << cc2d.Restitution;
            out << YAML::Key << "RestitutionThreshold" << YAML::Value << cc2d.RestitutionThreshold;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<DirectionalLightComponent>())
        {
            out << YAML::Key << "DirectionalLightComponent";
            out << YAML::BeginMap;

            auto& dl = entity.GetComponent<DirectionalLightComponent>();
            out << YAML::Key << "Direction"  << YAML::Value << dl.Direction;
            out << YAML::Key << "Color"      << YAML::Value << dl.Color;
            out << YAML::Key << "Intensity"  << YAML::Value << dl.Intensity;
            out << YAML::Key << "CastShadow" << YAML::Value << dl.CastShadow;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<PointLightComponent>())
        {
            out << YAML::Key << "PointLightComponent";
            out << YAML::BeginMap;

            auto& pl = entity.GetComponent<PointLightComponent>();
            out << YAML::Key << "Color"      << YAML::Value << pl.Color;
            out << YAML::Key << "Intensity"  << YAML::Value << pl.Intensity;
            out << YAML::Key << "Radius"     << YAML::Value << pl.Radius;
            out << YAML::Key << "CastShadow" << YAML::Value << pl.CastShadow;

            out << YAML::EndMap;
        }
        if (entity.HasComponent<ParticleEmitterComponent>())
        {
            out << YAML::Key << "ParticleEmitterComponent";
            out << YAML::BeginMap;

            auto& pe = entity.GetComponent<ParticleEmitterComponent>();
            out << YAML::Key << "ColorBegin"        << YAML::Value << pe.ColorBegin;
            out << YAML::Key << "ColorEnd"          << YAML::Value << pe.ColorEnd;
            out << YAML::Key << "SizeBegin"         << YAML::Value << pe.SizeBegin;
            out << YAML::Key << "SizeEnd"           << YAML::Value << pe.SizeEnd;
            out << YAML::Key << "SizeVariation"     << YAML::Value << pe.SizeVariation;
            out << YAML::Key << "Velocity"          << YAML::Value << pe.Velocity;
            out << YAML::Key << "VelocityVariation" << YAML::Value << pe.VelocityVariation;
            out << YAML::Key << "LifeTime"          << YAML::Value << pe.LifeTime;
            out << YAML::Key << "EmissionRate"      << YAML::Value << pe.EmissionRate;
            out << YAML::Key << "MaxParticles"      << YAML::Value << pe.MaxParticles;
            out << YAML::Key << "Active"            << YAML::Value << pe.Active;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<TextureAnimationComponent>())
        {
            out << YAML::Key << "TextureAnimationComponent";
            out << YAML::BeginMap;

            auto& ta = entity.GetComponent<TextureAnimationComponent>();
            out << YAML::Key << "Columns"        << YAML::Value << ta.Columns;
            out << YAML::Key << "Rows"           << YAML::Value << ta.Rows;
            out << YAML::Key << "FrameCount"     << YAML::Value << ta.FrameCount;
            out << YAML::Key << "AnimationSpeed" << YAML::Value << ta.AnimationSpeed;
            out << YAML::Key << "Loop"           << YAML::Value << ta.Loop;

            out << YAML::EndMap;
        }
        // 结束实体的 Map
        out << YAML::EndMap;
    }

	void SceneSerializer::Serialize(const std::string& filepath)
	{
		YAML::Emitter out;
		out << YAML::BeginMap;
		out << YAML::Key << "Scene" << YAML::Value << "Unamed Scene";
		if (!m_Scene->GetEnvironment().empty())
		{
			const std::filesystem::path environment = std::filesystem::absolute(m_Scene->GetEnvironment());
			const std::filesystem::path folder = std::filesystem::absolute(filepath).parent_path();
			const std::filesystem::path relative = environment.lexically_relative(folder);
			out << YAML::Key << "Environment" << YAML::Value << (relative.empty() ? environment : relative).generic_u8string();
		}
		out << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;
		// 使用 view 或 storage 来遍历实体
		const auto folder = std::filesystem::absolute(filepath).parent_path();
		auto view = m_Scene->m_Registry.view<entt::entity>();
		for (auto entityID : view)
		{
			Entity entity{ entityID, m_Scene.get() };
			SerializeEntity(out, entity, folder);
		}
		out << YAML::EndSeq;
		out << YAML::EndMap;

		FileSystem::WriteText(filepath, out.c_str());
	}

	void SceneSerializer::SerializeRuntime(const std::string& filepath)
	{
		//TODO:Implemented
		RF_CORE_ASSERT(false);
	}

	bool SceneSerializer::Deserialize(const std::string& filepath) try
	{
        std::ifstream stream(filepath);
        std::stringstream strStream;
        strStream << stream.rdbuf();

        YAML::Node data = YAML::Load(strStream.str());
        if (!data["Scene"])
            return false;

        std::filesystem::path environment;
        if (data["Environment"])
            environment = (std::filesystem::absolute(filepath).parent_path() /
                std::filesystem::u8path(data["Environment"].as<std::string>())).lexically_normal();
        m_Scene->SetEnvironment(environment);

        AssetManager::GetLibrary().Scan("assets");

        std::string sceneName = data["Scene"].as<std::string>();
        RF_CORE_TRACE("Deserializing Scene'{0}", sceneName);

        auto entities = data["Entities"];
        if (entities)
        {
            for (auto entity : entities)
            {
                uint64_t uuid = entity["Entity"].as<uint64_t>(); 

                std::string name;
                if (entity["TagComponent"])
                {
                    name = entity["TagComponent"]["Tag"].as<std::string>();
                }

                Entity deserializedEntity = m_Scene->CreateEntityWithUUID(uuid,name);

                // Transform Component
                if (entity["TransformComponent"])
                {
                    auto tcNode = entity["TransformComponent"];
                    auto& tc = deserializedEntity.GetComponent<TransformComponent>();
                    tc.Translation = tcNode["Translation"].as<glm::vec3>();
                    tc.Rotation = tcNode["Rotation"].as<glm::vec3>();
                    tc.Scale = tcNode["Scale"].as<glm::vec3>();
                }

                // Camera Component
                if (entity["CameraComponent"])
                {
                    auto ccNode = entity["CameraComponent"];
                    auto& cameraComp = deserializedEntity.AddComponent<CameraComponent>();
                    auto& cameraNode = ccNode["Camera"];

                    cameraComp.Camera.SetProjectionType((SceneCamera::ProjectionType)cameraNode["ProjectionType"].as<int>());
                    cameraComp.Camera.SetPerspectiveFOV(cameraNode["PerspectiveFov"].as<float>());
                    cameraComp.Camera.SetPerspectiveNearClip(cameraNode["PerspectiveNear"].as<float>());
                    cameraComp.Camera.SetPerspectiveFarClip(cameraNode["PerspectiveFar"].as<float>());
                    cameraComp.Camera.SetOrthographicSize(cameraNode["OrthographicSize"].as<float>());
                    cameraComp.Camera.SetOrthographicNearClip(cameraNode["OrthographicNear"].as<float>());
                    cameraComp.Camera.SetOrthographicFarClip(cameraNode["OrthographicFar"].as<float>());

                    cameraComp.Primary = ccNode["Primary"].as<bool>();
                    cameraComp.FixedAspectRatio = ccNode["FixedAspectRatio"].as<bool>();
                }

                // ScriptComponent
                if (entity["ScriptComponent"])
                {
                    auto scNode = entity["ScriptComponent"];
                    auto& sc = deserializedEntity.AddComponent<ScriptComponent>();  // 修正：用 AddComponent 而不是 GetComponent
                    sc.ClassName = scNode["Class"].as<std::string>();  // 修正：ClassName → Name

                    if (scNode["ScriptFields"])
                    {
                        auto& fieldCache = ScriptEngine::GetScriptFieldCache(deserializedEntity.GetUUID());
                        for (auto fieldNode : scNode["ScriptFields"])
                        {
                            if (!fieldNode["Name"] || !fieldNode["Type"] || !fieldNode["Value"])
                                continue;

                            std::string fieldName = fieldNode["Name"].as<std::string>();
                            ScriptFieldType fieldType = ScriptFieldTypeFromString(fieldNode["Type"].as<std::string>());
                            std::vector<uint8_t> bytes;
                            if (DeserializeScriptFieldValue(fieldNode["Value"], fieldType, bytes))
                                fieldCache[fieldName] = std::move(bytes);
                        }
                    }
                }

                // Sprite Renderer Component
                if (entity["SpriteRendererComponent"])
                {
                    auto srNode = entity["SpriteRendererComponent"];
                    auto& src = deserializedEntity.AddComponent<SpriteRendererComponent>();
                    src.Color = srNode["Color"].as<glm::vec4>();
                    if (srNode["TilingFactor"])
                        src.TilingFactor = srNode["TilingFactor"].as<float>();
                    if (srNode["TexturePath"])
                    {
                        std::string texPath = srNode["TexturePath"].as<std::string>();
                        if (!texPath.empty())
                            src.Texture = Texture2D::Create(texPath);
                    }
                }

                // Circle Renderer Component
                if (entity["CircleRendererComponent"])
                {
                    auto crNode = entity["CircleRendererComponent"];
                    auto& crc = deserializedEntity.AddComponent<CircleRendererComponent>();
                    crc.Color     = crNode["Color"].as<glm::vec4>();
                    crc.Thickness = crNode["Thickness"].as<float>();
                    crc.Fade      = crNode["Fade"].as<float>();
                }

                // RigidBody2DComponent
                if (entity["RigidBodyComponent"])
                {
                    auto rbNode = entity["RigidBodyComponent"];
                    auto& rb2d = deserializedEntity.AddComponent<RigidBody2DComponent>();

                    rb2d.Type = ReadBodyType<RigidBody2DComponent::BodyType>(rbNode["Body Type"].as<std::string>());
                    rb2d.FixedRotation = rbNode["Fixed Rotation"].as<bool>();
                }

                if (entity["PickupComponent"])
                    deserializedEntity.AddComponent<PickupComponent>();

                if (const YAML::Node animation = entity["SkeletonAnimationComponent"])
                {
                    SkeletonAnimationComponent& player = deserializedEntity.AddComponent<SkeletonAnimationComponent>();
                    player.Playing = animation["Playing"].as<bool>(true);
                    const auto file = std::filesystem::u8path(animation["File"].as<std::string>());
                    if (!file.empty())
                    {
                        player.File = (std::filesystem::absolute(filepath).parent_path() / file).lexically_normal();
                        try { player.Load(player.File); }
                        catch (const std::exception& error) { RF_CORE_WARN("BVH: {0}", error.what()); }
                        if (player.Clip) player.Seek(animation["Time"].as<float>(0.f));
                    }
                }

                if (const YAML::Node bodyNode = entity["RigidBody3DComponent"])
                {
                    if (deserializedEntity.HasComponent<RigidBody2DComponent>())
                        throw std::runtime_error("An entity cannot use both a 2D and a 3D rigid body");
                    RigidBody3DComponent& body = deserializedEntity.AddComponent<RigidBody3DComponent>();
                    body.Type = ReadBodyType<BodyType3D>(bodyNode["BodyType"].as<std::string>());
                    body.Density = bodyNode["Density"].as<float>();
                    if (!(body.Density > 0.f))
                        throw std::runtime_error("3D rigid body density must be positive");
                }

                if (const YAML::Node boxNode = entity["BoxCollider3DComponent"])
                {
                    BoxCollider3DComponent& box = deserializedEntity.AddComponent<BoxCollider3DComponent>();
                    box.HalfSize = boxNode["HalfSize"].as<glm::vec3>();
                    if (!glm::all(glm::greaterThan(box.HalfSize, glm::vec3(0))))
                        throw std::runtime_error("3D collision box half sizes must be positive");
                }

                //BoxCollider2DComponent
                if (entity["BoxCollider2DComponent"])
                {
                    auto bcNode = entity["BoxCollider2DComponent"];
                    auto& bc2d = deserializedEntity.AddComponent<BoxCollider2DComponent>();

                    bc2d.Offset = bcNode["Offset"].as<glm::vec2>();
                    bc2d.Size = bcNode["Size"].as<glm::vec2>();
                    bc2d.Density = bcNode["Density"].as<float>();
                    bc2d.Friction = bcNode["Friction"].as<float>();
                    bc2d.Restitution = bcNode["Restitution"].as<float>();
                    bc2d.RestitutionThreshold = bcNode["RestitutionThreshold"].as<float>();
                }

                // CircleCollider2DComponent
                if (entity["CircleCollider2DComponent"])
                {
                    auto ccNode = entity["CircleCollider2DComponent"];
                    auto& cc2d = deserializedEntity.AddComponent<CircleCollider2DComponent>();

                    cc2d.Offset               = ccNode["Offset"].as<glm::vec2>();
                    cc2d.Radius               = ccNode["Radius"].as<float>();
                    cc2d.Density              = ccNode["Density"].as<float>();
                    cc2d.Friction             = ccNode["Friction"].as<float>();
                    cc2d.Restitution          = ccNode["Restitution"].as<float>();
                    cc2d.RestitutionThreshold = ccNode["RestitutionThreshold"].as<float>();
                }

                // MeshRendererComponent
                if (entity["MeshRendererComponent"])
                {
                    auto mrNode = entity["MeshRendererComponent"];
                    auto& mr = deserializedEntity.AddComponent<MeshRendererComponent>();
                    mr.Color = mrNode["Color"].as<glm::vec4>();
                    // 目前只支持立方体
                    mr.MeshData = Mesh::CreateCube();

                    // 旧场景只保存网格颜色。先用它创建材质，文件里另有 BaseColor 时再读入材质自己的颜色。
                    mr.Mat = Material::Create(mr.Color);
                    ReadMaterial(mrNode, *mr.Mat);
                }

                // ModelRendererComponent
                if (entity["ModelRendererComponent"])
                {
                    auto mrNode = entity["ModelRendererComponent"];
                    auto& mr = deserializedEntity.AddComponent<ModelRendererComponent>();
                    ReadModelFile(mrNode, mr.ModelID, mr.FilePath);
                    float previousHeight = 1.0f;
                    for (const auto& entry : mrNode["LODs"])
                    {
                        ModelLOD lod;
                        ReadModelFile(entry, lod.ModelID, lod.FilePath);
                        lod.ScreenHeight = entry["ScreenHeight"].as<float>();
                        if (!(lod.ScreenHeight > 0 && lod.ScreenHeight < previousHeight))
                            throw std::runtime_error("LOD screen heights must decrease from 1 to 0: " + mr.FilePath);
                        previousHeight = lod.ScreenHeight;
                        mr.LODs.push_back(std::move(lod));
                    }
                    mr.Color    = mrNode["Color"].as<glm::vec4>();
                    // ModelAsset 将在首次渲染时由 AssetManager 懒加载

                    // per-submesh 覆盖材质
                    if (mrNode["OverrideMaterials"])
                    {
                        auto matsNode = mrNode["OverrideMaterials"];
                        for (size_t i = 0; i < matsNode.size(); i++)
                        {
                            auto matNode = matsNode[i];
                            // 空项保留子网格原本的材质。
                            Ref<Material> material;
                            if (matNode.size())
                            {
                                material = Material::Create();
                                const Material* original = nullptr;
                                if (matNode["ModelMaps"])
                                {
                                    if (!mr.ModelAsset) mr.ModelAsset = AssetManager::Load<Model>(mr.ModelID);
                                    if (!mr.ModelAsset || i >= mr.ModelAsset->GetSubMeshes().size())
                                        throw std::runtime_error("Cannot restore material textures from " + mr.FilePath);
                                    original = mr.ModelAsset->GetSubMeshes()[i].Mat.get();
                                }
                                ReadMaterial(matNode, *material, original);
                            }
                            mr.OverrideMaterials.push_back(material);
                        }
                    }
                    // 向后兼容旧格式 (单一覆盖材质)
                    else if (mrNode["HasOverrideMaterial"] && mrNode["HasOverrideMaterial"].as<bool>())
                    {
                        auto mat = Material::Create(glm::vec4(1.0f));
                        if (mrNode["OverrideBaseColor"]) mat->BaseColor = mrNode["OverrideBaseColor"].as<glm::vec4>();
                        if (mrNode["OverrideMetallic"])  mat->Metallic  = mrNode["OverrideMetallic"].as<float>();
                        if (mrNode["OverrideRoughness"]) mat->Roughness = mrNode["OverrideRoughness"].as<float>();
                        if (mrNode["OverrideAO"])        mat->AO        = mrNode["OverrideAO"].as<float>();
                        mr.OverrideMaterials.push_back(mat); // 单一材质放入数组
                    }
                }

                // DirectionalLightComponent
                if (entity["DirectionalLightComponent"])
                {
                    auto dlNode = entity["DirectionalLightComponent"];
                    auto& dl = deserializedEntity.AddComponent<DirectionalLightComponent>();
                    dl.Direction  = dlNode["Direction"].as<glm::vec3>();
                    dl.Color      = dlNode["Color"].as<glm::vec3>();
                    dl.Intensity  = dlNode["Intensity"].as<float>();
                    dl.CastShadow = dlNode["CastShadow"].as<bool>();
                }

                // PointLightComponent
                if (entity["PointLightComponent"])
                {
                    auto plNode = entity["PointLightComponent"];
                    auto& pl = deserializedEntity.AddComponent<PointLightComponent>();
                    pl.Color      = plNode["Color"].as<glm::vec3>();
                    pl.Intensity  = plNode["Intensity"].as<float>();
                    pl.Radius     = plNode["Radius"].as<float>();
                    pl.CastShadow = plNode["CastShadow"].as<bool>();
                }

                // ParticleEmitterComponent
                if (entity["ParticleEmitterComponent"])
                {
                    auto peNode = entity["ParticleEmitterComponent"];
                    auto& pe = deserializedEntity.AddComponent<ParticleEmitterComponent>();
                    pe.ColorBegin        = peNode["ColorBegin"].as<glm::vec4>();
                    pe.ColorEnd          = peNode["ColorEnd"].as<glm::vec4>();
                    pe.SizeBegin         = peNode["SizeBegin"].as<float>();
                    pe.SizeEnd           = peNode["SizeEnd"].as<float>();
                    pe.SizeVariation     = peNode["SizeVariation"].as<float>();
                    pe.Velocity          = peNode["Velocity"].as<glm::vec2>();
                    pe.VelocityVariation = peNode["VelocityVariation"].as<glm::vec2>();
                    pe.LifeTime          = peNode["LifeTime"].as<float>();
                    pe.EmissionRate      = peNode["EmissionRate"].as<float>();
                    pe.MaxParticles      = peNode["MaxParticles"].as<uint32_t>();
                    pe.Active            = peNode["Active"].as<bool>();
                }

                // TextureAnimationComponent
                if (entity["TextureAnimationComponent"])
                {
                    auto taNode = entity["TextureAnimationComponent"];
                    auto& ta = deserializedEntity.AddComponent<TextureAnimationComponent>();
                    ta.Columns        = taNode["Columns"].as<int>();
                    ta.Rows           = taNode["Rows"].as<int>();
                    ta.FrameCount     = taNode["FrameCount"].as<int>();
                    ta.AnimationSpeed = taNode["AnimationSpeed"].as<float>();
                    ta.Loop           = taNode["Loop"].as<bool>();
                }
            }
        }
        return true;
	}

    catch (const std::exception& error)
    {
        RF_CORE_ERROR("Cannot open scene {0}: {1}", filepath, error.what());
        return false;
    }

	bool SceneSerializer::DeserializeRuntime(const std::string& filepath)
	{
		//TODO:Implemented
		RF_CORE_ASSERT(false);
		return false;
	}
}
