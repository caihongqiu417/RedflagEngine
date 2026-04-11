#pragma once

#include<glm/glm.hpp>
#include<glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include<glm/gtx/quaternion.hpp>
#include"SceneCamera.h"
#include "Redflag/Renderer/SubTexture2D.h"
#include"Redflag/Renderer/Texture.h"
#include"Redflag/Core/Timestep.h"
#include"Redflag/Core/UUID.h"
#include"Redflag/Renderer/Mesh.h"
#include"Redflag/Renderer/Model.h"

namespace Redflag {

	struct IDComponent
	{
		UUID ID;

		IDComponent() = default;
		IDComponent(const IDComponent&) = default;
	};

	struct TransformComponent
	{
		glm::vec3 Translation = { 0.0f,0.0f,0.0f };
		glm::vec3 Rotation = { 0.0f,0.0f,0.0f };
		glm::vec3 Scale = { 1.0f,1.0f,1.0f };

		TransformComponent() = default;
		TransformComponent(const TransformComponent& other) = default;  //Ĭ�Ͽ�������Ϊ��Աû��ָ�룬����ǳ��������
		TransformComponent(const glm::vec3& translation)
			:Translation(translation){}

		glm::mat4 GetTransform()const
		{
			glm::mat4 rotation = glm::toMat4(glm::quat(Rotation));

			return glm::translate(glm::mat4(1.0f), Translation) * rotation * glm::scale(glm::mat4(1.0f), Scale);
		}
	};

	struct SpriteRendererComponent
	{
		glm::vec4 Color = glm::vec4(1.0f);
		Ref<Texture2D> Texture;
		float TilingFactor = 1.f;

		SpriteRendererComponent() = default;
		SpriteRendererComponent(const SpriteRendererComponent& other) = default;
		SpriteRendererComponent(const glm::vec4& color)
			:Color(color){}
	};

	struct TagComponent
	{
		std::string Tag;

		TagComponent() = default;
		TagComponent(const TagComponent& other) = default;
		TagComponent(const std::string& tag)
			:Tag(tag){}
	};

	struct CameraComponent
	{
		SceneCamera Camera;
		bool Primary = true;
		bool FixedAspectRatio = false; //Ĭ�ϲ��ã�ֻ���ӿ������Ҫ�ı���߱�

		CameraComponent() = default;
		CameraComponent(const CameraComponent& other) = default;
	};

	class ScriptableEntity;

	struct NativeScriptComponent
	{
		ScriptableEntity* Instance = nullptr;

		// ����ָ��
		ScriptableEntity* (*InstantiateScript)();  
		void (*DestroyScript)(NativeScriptComponent*);

		template<typename T>
		void Bind()
		{
			// lambda���뺯��ָ�� - ʹ����ȷ�ı�����
			InstantiateScript = []() -> ScriptableEntity* {
				return static_cast<ScriptableEntity*>(new T());
				};

			DestroyScript = [](NativeScriptComponent* nsc) {
				delete static_cast<ScriptableEntity*>(nsc->Instance);
				nsc->Instance = nullptr;
				};
		}
	};

	//����
	struct RigidBody2DComponent
	{
		enum class BodyType{Static=0,Dynamic,Kinematic};
		BodyType Type = BodyType::Static;
		bool FixedRotation = false;

		//�洢Box2D��Ϣ
		void* RuntimeBody = nullptr;

		RigidBody2DComponent() = default;
		RigidBody2DComponent(const RigidBody2DComponent&) = default;
	};

	struct BoxCollider2DComponent
	{
		glm::vec2 Offset = { 0.f,0.f };
		glm::vec2 Size = { 0.5f,0.5f };

		//TODO��������������
		float Density = 1.f;
		float Friction = 0.5f;
		float Restitution = 0.f;
		float RestitutionThreshold = 0.5f; //ֻ�е������� �����ײ�ٶ� > 0.5 ��/�� ʱ���������������õĵ��Լ��㡣

		//�洢Box2D��Ϣ
		void* RuntimeFixture = nullptr;

		BoxCollider2DComponent() = default;
		BoxCollider2DComponent(const BoxCollider2DComponent&) = default;
	};

	// 3D 网格渲染组件
	struct MeshRendererComponent
	{
		Ref<Mesh>      MeshData;
		glm::vec4      Color = glm::vec4(1.0f);
		Ref<Texture2D> Texture;

		MeshRendererComponent() = default;
		MeshRendererComponent(const MeshRendererComponent&) = default;
	};

	// 3D 模型渲染组件（通过 Assimp 从文件加载，支持多子网格）
	struct ModelRendererComponent
	{
		std::string  FilePath;       // 模型文件路径，场景序列化时保存此字段
		Ref<Model>   ModelAsset;     // 运行时句柄，由 AssetManager 管理，不序列化

		glm::vec4    Color = glm::vec4(1.0f); // 无贴图时的备用颜色

		ModelRendererComponent() = default;
		ModelRendererComponent(const ModelRendererComponent&) = default;
	};
}