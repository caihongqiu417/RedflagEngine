#pragma once

#include<glm/glm.hpp>
#include"Redflag/Renderer/Camera.h"

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
		Camera Camera;
		bool Primary = true;

		CameraComponent() = default;
		CameraComponent(const CameraComponent& other) = default;
		CameraComponent(const glm::mat4& projection)
			:Camera(projection) {}
	};
}