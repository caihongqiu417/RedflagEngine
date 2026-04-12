#include"rfpch.h"

#include"SceneSerializer.h"
#include <yaml-cpp/yaml.h>

#include"Entity.h"
#include"Components.h"
#include "Redflag/Core/AssetManager.h"
#include "Redflag/Renderer/Model.h"

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

    static std::string RigidBody2DBodyTypeToString(RigidBody2DComponent::BodyType bodyType)
    {
        switch (bodyType)
        {
        case RigidBody2DComponent::BodyType::Static:    return "Static";
        case RigidBody2DComponent::BodyType::Dynamic:   return "Dynamic";
        case RigidBody2DComponent::BodyType::Kinematic: return "Kinematic";
        default:                                        return "Static";
        }
    }

    static RigidBody2DComponent::BodyType RigidBody2DBodyTypeFromString(const std::string& bodyTypeString)
    {
        if (bodyTypeString == "Static")    return RigidBody2DComponent::BodyType::Static;
        if (bodyTypeString == "Dynamic")   return RigidBody2DComponent::BodyType::Dynamic;
        if (bodyTypeString == "Kinematic") return RigidBody2DComponent::BodyType::Kinematic;

        return RigidBody2DComponent::BodyType::Static;
    }

    static void SerializeEntity(YAML::Emitter& out, Entity entity)
    {
        RF_CORE_ASSERT(entity.HasComponent<IDComponent>());
        // ��ʼʵ��� Map
        out << YAML::BeginMap;
        out << YAML::Key << "Entity" << YAML::Value << entity.GetUUID();

        if (entity.HasComponent<TagComponent>())
        {
            out << YAML::Key << "TagComponent";
            out << YAML::BeginMap;

            auto& tag = entity.GetComponent<TagComponent>().Tag;
            out << YAML::Key << "Tag" << YAML::Value << tag;

            out << YAML::EndMap; // ���� TagComponent Map
        }

        if (entity.HasComponent<TransformComponent>())
        {
            out << YAML::Key << "TransformComponent";
            out << YAML::BeginMap;

            auto& tc = entity.GetComponent<TransformComponent>();
            out << YAML::Key << "Translation" << YAML::Value << tc.Translation;
            out << YAML::Key << "Rotation" << YAML::Value << tc.Rotation;
            out << YAML::Key << "Scale" << YAML::Value << tc.Scale;

            out << YAML::EndMap; // ���� TransformComponent Map
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
            out << YAML::EndMap; // ���� Camera Map

            out << YAML::Key << "Primary" << YAML::Value << cameraComponent.Primary;
            out << YAML::Key << "FixedAspectRatio" << YAML::Value << cameraComponent.FixedAspectRatio;

            out << YAML::EndMap; // ���� CameraComponent Map
        }

        if (entity.HasComponent<SpriteRendererComponent>())
        {
            out << YAML::Key << "SpriteRendererComponent";
            out << YAML::BeginMap;

            auto& spriteRendererComponent = entity.GetComponent<SpriteRendererComponent>();
            out << YAML::Key << "Color" << YAML::Value << spriteRendererComponent.Color;

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

            out << YAML::EndMap;
        }

        if (entity.HasComponent<ModelRendererComponent>())
        {
            out << YAML::Key << "ModelRendererComponent";
            out << YAML::BeginMap;

            auto& mr = entity.GetComponent<ModelRendererComponent>();
            out << YAML::Key << "FilePath" << YAML::Value << mr.FilePath;
            out << YAML::Key << "Color"    << YAML::Value << mr.Color;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<RigidBody2DComponent>())
        {
            out << YAML::Key << "RigidBodyComponent";
            out << YAML::BeginMap;

            auto& rb2dComponent = entity.GetComponent<RigidBody2DComponent>();
            out << YAML::Key << "Body Type" << YAML::Value <<RigidBody2DBodyTypeToString(rb2dComponent.Type);
            out << YAML::Key << "Fixed Rotation" << YAML::Value << rb2dComponent.FixedRotation;

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

        // ����ʵ��� Map
        out << YAML::EndMap;
    }

	void SceneSerializer::Serialize(const std::string& filepath)
	{
		YAML::Emitter out;
		out << YAML::BeginMap;
		out << YAML::Key << "Scene" << YAML::Value << "Unamed Scene";
		out << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;
		// ʹ�� view �� storage ������ʵ��
		auto view = m_Scene->m_Registry.view<entt::entity>();
		for (auto entityID : view)
		{
			Entity entity{ entityID, m_Scene.get() };
			SerializeEntity(out, entity);
		}
		out << YAML::EndSeq;
		out << YAML::EndMap;

		std::ofstream fout(filepath);
		fout << out.c_str();
	}

	void SceneSerializer::SerializeRuntime(const std::string& filepath)
	{
		//TODO:Implemented
		RF_CORE_ASSERT(false);
	}

	bool SceneSerializer::Deserialize(const std::string& filepath)
	{
        std::ifstream stream(filepath);
        std::stringstream strStream;
        strStream << stream.rdbuf();

        YAML::Node data = YAML::Load(strStream.str());
        if (!data["Scene"])
            return false;

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

                // Sprite Renderer Component
                if (entity["SpriteRendererComponent"])
                {
                    auto srNode = entity["SpriteRendererComponent"];
                    auto& src = deserializedEntity.AddComponent<SpriteRendererComponent>();
                    src.Color = srNode["Color"].as<glm::vec4>();
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

                    rb2d.Type = RigidBody2DBodyTypeFromString(rbNode["Body Type"].as<std::string>());
                    rb2d.FixedRotation = rbNode["Fixed Rotation"].as<bool>();
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
                }

                // ModelRendererComponent
                if (entity["ModelRendererComponent"])
                {
                    auto mrNode = entity["ModelRendererComponent"];
                    auto& mr = deserializedEntity.AddComponent<ModelRendererComponent>();
                    mr.FilePath = mrNode["FilePath"].as<std::string>();
                    mr.Color    = mrNode["Color"].as<glm::vec4>();
                    // ModelAsset 将在首次渲染时由 AssetManager 懒加载
                }
            }
        }
        return true;
	}

	bool SceneSerializer::DeserializeRuntime(const std::string& filepath)
	{
		//TODO:Implemented
		RF_CORE_ASSERT(false);
		return false;
	}
}
