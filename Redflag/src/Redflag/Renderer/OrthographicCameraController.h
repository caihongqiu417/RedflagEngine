#pragma once

#include "Redflag/Renderer/OrthographicCamera.h"
#include"Redflag/Core/Timestep.h"

#include"Redflag/Events/ApplicationEvent.h"
#include"Redflag/Events/MouseEvent.h"
#include"Redflag/Events/Event.h"

namespace Redflag {

	struct OrthographicCameraBounds
	{
		float Left, Right;
		float Bottom, Top;

		float GetWidth() const { return Right - Left; }
		float GetHeight() const { return Top - Bottom; }
	};

	class OrthographicCameraController
	{
	public:
		OrthographicCameraController(float aspectRatio, bool rotation = false);
		void OnUpdate(Timestep ts);
		void OnEvent(Event& e);

		void OnResize(float width, float height);

		const OrthographicCamera& GetCamera() const { return m_Camera; }
		OrthographicCamera& GetCamera() { return m_Camera; }

		float GetZoomLevel() const { return m_ZoomLevel; }
		void SetZoomLevel(float level) { m_ZoomLevel = level; CalculateView(); }

		const OrthographicCameraBounds& GetBounds() const { return m_Bounds; }	
	private:
		void CalculateView();

		bool OnMouseScrolled(MouseScrolledEvent& e);
		bool OnWindowResized(WindowResizeEvent& e);
	private:
		float m_AspectRatio;
		float m_ZoomLevel = 1.0f;
		OrthographicCamera m_Camera;
		OrthographicCameraBounds m_Bounds;
		
		bool m_Rotation;

		glm::vec3 m_CameraPosition = { 0.0f,0.0f,0.0f };
		float m_CameraRotation = 0.0f;
		float m_CameraTranslationSpeed = 6.0f;
		float m_CameraRotationSpeed = 50.0f;
	};
}
