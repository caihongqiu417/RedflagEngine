#pragma once

#include<glm/glm.hpp>
#include<glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include<glm/gtx/quaternion.hpp>
#include"SceneCamera.h"
#include"ScriptableEntity.h"
#include "Redflag/Renderer/SubTexture2D.h"
#include"Redflag/Renderer/Texture.h"
#include"Redflag/Core/Timestep.h"

namespace Redflag {

	struct TransformComponent
	{
		glm::vec3 Translation = { 0.0f,0.0f,0.0f };
		glm::vec3 Rotation = { 0.0f,0.0f,0.0f };
		glm::vec3 Scale = { 1.0f,1.0f,1.0f };

		TransformComponent() = default;
		TransformComponent(const TransformComponent& other) = default;  //默认拷贝，因为成员没有指针，所以浅拷贝即可
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
		bool FixedAspectRatio = false; //默认不用，只有视口相机需要改变宽高比

		CameraComponent() = default;
		CameraComponent(const CameraComponent& other) = default;
	};

	struct NativeScriptComponent
	{
		ScriptableEntity* Instance = nullptr;

		// 函数指针
		ScriptableEntity* (*InstantiateScript)();  
		void (*DestroyScript)(NativeScriptComponent*);

		template<typename T>
		void Bind()
		{
			// lambda存入函数指针 - 使用正确的变量名
			InstantiateScript = []() -> ScriptableEntity* {
				return static_cast<ScriptableEntity*>(new T());
				};

			DestroyScript = [](NativeScriptComponent* nsc) {
				delete static_cast<ScriptableEntity*>(nsc->Instance);
				nsc->Instance = nullptr;
				};
		}
	};

	//物理
	struct RigidBody2DComponent
	{
		enum class BodyType{Static=0,Dynamic,Kinematic};
		BodyType Type = BodyType::Static;
		bool FixedRotation = false;

		//存储Box2D信息
		void* RuntimeBody = nullptr;

		RigidBody2DComponent() = default;
		RigidBody2DComponent(const RigidBody2DComponent&) = default;
	};

	struct BoxCollider2DComponent
	{
		glm::vec2 Offset = { 0.f,0.f };
		glm::vec2 Size = { 0.5f,0.5f };

		//TODO：放入物理材质
		float Density = 1.f;
		float Friction = 0.5f;
		float Restitution = 0.f;
		float RestitutionThreshold = 0.5f; //只有当两物体 相对碰撞速度 > 0.5 米/秒 时，才启用上面设置的弹性计算。

		//存储Box2D信息
		void* RuntimeFixture = nullptr;

		BoxCollider2DComponent() = default;
		BoxCollider2DComponent(const BoxCollider2DComponent&) = default;
	};
}