#include"rfpch.h"
#include"OrthographicCameraController.h"

#include"Redflag/Core/Input.h"
#include"Redflag/Core/KeyCode.h"

namespace Redflag {

	OrthographicCameraController::OrthographicCameraController(float aspectRatio, bool rotation)
		:m_AspectRatio(aspectRatio),m_Camera(OrthographicCamera(-aspectRatio * m_ZoomLevel, aspectRatio* m_ZoomLevel, -m_ZoomLevel, m_ZoomLevel)), m_Rotation(rotation)
	{

	}

	void OrthographicCameraController::OnUpdate(Timestep ts)
	{
		//根据输入实现相机移动和旋转
		if (Input::IsKeyPressed(RF_KEY_A))
			m_CameraPosition.x += m_CameraTranslationSpeed * ts;
		if (Input::IsKeyPressed(RF_KEY_D))
			m_CameraPosition.x -= m_CameraTranslationSpeed * ts;
		if (Input::IsKeyPressed(RF_KEY_W))
			m_CameraPosition.y -= m_CameraTranslationSpeed * ts;
		if (Input::IsKeyPressed(RF_KEY_S))
			m_CameraPosition.y += m_CameraTranslationSpeed * ts;

		if (m_Rotation)
		{
			if (Input::IsKeyPressed(RF_KEY_Q))
				m_CameraRotation -= m_CameraRotationSpeed * ts;
			if (Input::IsKeyPressed(RF_KEY_E))
				m_CameraRotation += m_CameraRotationSpeed * ts;

			m_Camera.SetRotation(m_CameraRotation);
		}

		m_Camera.SetPosition(m_CameraPosition);
	}

	void OrthographicCameraController::OnEvent(Event& e)
	{
		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<MouseScrolledEvent>(std::bind(&OrthographicCameraController::OnMouseScrolled, this, std::placeholders::_1));
		dispatcher.Dispatch<WindowResizeEvent>(std::bind(&OrthographicCameraController::OnWindowResized, this, std::placeholders::_1));
	}

	bool OrthographicCameraController::OnMouseScrolled(MouseScrolledEvent& e)
	{
		m_ZoomLevel -= e.GetYOffset() * 0.25f;
		m_Camera.SetProjection(-m_AspectRatio * m_ZoomLevel, m_AspectRatio * m_ZoomLevel, -m_ZoomLevel, m_ZoomLevel);
		return false;
	}
	bool OrthographicCameraController::OnWindowResized(WindowResizeEvent& e)
	{
		m_AspectRatio = (float)e.GetWidth() / (float)e.GetHeight();
		m_Camera.SetProjection(-m_AspectRatio * m_ZoomLevel, m_AspectRatio * m_ZoomLevel, -m_ZoomLevel, m_ZoomLevel);
		return false;
	}
}