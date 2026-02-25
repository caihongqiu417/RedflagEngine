#pragma once
#include"glm/glm.hpp"

namespace Redflag {

	class Camera
	{
	public:
		Camera() = default;
		Camera(glm::mat4 projection)
			:m_Projection(projection){ }

		virtual ~Camera() = default;

		glm::mat4 GetProjection()const { return m_Projection; }

	protected:
		glm::mat4 m_Projection=glm::mat4(1.0f);
	};
}
