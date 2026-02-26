#pragma once

#include"Redflag/Renderer/Camera.h"

namespace Redflag {

	class SceneCamera :public Camera
	{
	public:
		enum class ProjectionType { Perspective = 0, Orthographic = 1 };
	public:
		SceneCamera();
		virtual ~SceneCamera() = default;

		// 正交相机相关
		void SetOrthographic(float size, float nearClip, float farClip);
		float GetOrthographicSize() const { return m_OrthographicSize; }
		void SetOrthographicSize(float size) { m_OrthographicSize = size; RecalculateProjection(); }
		float GetOrthographicNearClip()const { return m_OrthographicNear; }
		float GetOrthographicFarClip()const { return m_OrthographicFar; }
		void SetOrthographicNearClip(float nearClip) { m_OrthographicNear = nearClip; RecalculateProjection(); }
		void SetOrthographicFarClip(float farClip) { m_OrthographicFar = farClip; RecalculateProjection(); }

		// 透视相机相关
		float GetPerspectiveFOV() const { return m_PerspectiveFov; }
		void SetPerspectiveFOV(float fov) { m_PerspectiveFov = fov; RecalculateProjection(); }
		float GetPerspectiveNearClip() const { return m_PerspectiveNear; }
		float GetPerspectiveFarClip() const { return m_PerspectiveFar; }
		void SetPerspectiveNearClip(float nearClip) { m_PerspectiveNear = nearClip; RecalculateProjection(); }
		void SetPerspectiveFarClip(float farClip) { m_PerspectiveFar = farClip; RecalculateProjection(); }

		// 通用
		void SetViewportSize(uint32_t width, uint32_t height);
		ProjectionType GetProjectionType() const { return m_ProjectionType; }
		void SetProjectionType(ProjectionType type) { m_ProjectionType = type; RecalculateProjection(); }

	private:
		void RecalculateProjection();

	private:
		ProjectionType m_ProjectionType = ProjectionType::Orthographic;

		// 正交参数
		float m_OrthographicSize = 10.f;
		float m_OrthographicNear = -1.0f, m_OrthographicFar = 1.0f;

		// 透视参数
		float m_PerspectiveFov = glm::radians(45.0f);
		float m_PerspectiveNear = 0.01f, m_PerspectiveFar = 1000.f;

		float m_AspectRatio = 0.0f;
	};
}
