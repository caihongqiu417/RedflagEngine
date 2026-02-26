#pragma once

#include<glm/glm.hpp>
#include"SceneCamera.h"
#include"ScriptableEntity.h"
#include"Redflag/Core/Timestep.h"

namespace Redflag {

	struct TransformComponent
	{
		glm::mat4 Transform = glm::mat4(1.0f);

		TransformComponent() = default;
		TransformComponent(const TransformComponent& other) = default;  //默认拷贝，因为成员没有指针，所以浅拷贝即可
		TransformComponent(const glm::mat4& transform)
			:Transform(transform){}

		operator glm::mat4& () { return Transform; }
		operator const glm::mat4& () { return Transform; }
	};

	struct SpriteRendererComponent
	{
		glm::vec4 Color = glm::vec4(1.0f);

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
}