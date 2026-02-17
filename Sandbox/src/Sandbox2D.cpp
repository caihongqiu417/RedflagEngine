#include "rfpch.h"

#include "Redflag.h"


#include "Platform/OpenGL/OpenGLShader.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "Redflag/Renderer/Shader.h"

#include "Sandbox2D.h"



Sandbox2D::Sandbox2D()
    : Layer("Sandbox2D"), m_CameraController(1280.0f / 720.0f, true)
{
}

void Sandbox2D::OnAttach()
{
	m_RedCloudTexture = Redflag::Texture2D::Create("assets/textures/RedCloud.png");
}

void Sandbox2D::OnDetach()
{
}

void Sandbox2D::OnUpdate(Redflag::Timestep ts)
{
	RF_PROFILE_FUNCTION();
    
	{
		//updata camera
		RF_PROFILE_SCOPE("CameraController::OnUpdate");
		m_CameraController.OnUpdate(ts);
	}

	{
		//Render
		RF_PROFILE_SCOPE("RendererCommand::Clear");
		Redflag::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1 });
		Redflag::RenderCommand::Clear();
	}
   
	{
		//Render scene
		RF_PROFILE_SCOPE("Renderer2D::Draw");
		Redflag::Renderer2D::BeginScene(m_CameraController.GetCamera());

		// 负的值越大，渲染的越早（越靠后）
		Redflag::Renderer2D::DrawQuad({ 0.4f, 0.4f, -0.9f }, { 1.5f, 1.5f }, { 0.5f,0.8f,0.2f,1.0f });
		Redflag::Renderer2D::DrawQuad({ 0.0f, 0.0f, -0.8f }, { 1.0f, 1.0f }, m_RedCloudTexture);
		Redflag::Renderer2D::DrawQuad({ 0.2f, 0.2f, -0.5f }, { 1.0f, 1.0f }, m_SquareColor);
		Redflag::Renderer2D::DrawQuad({ 0.4f, 0.4f, -0.2f }, { 1.0f, 1.0f }, { 0.8f,0.2f,0.3f,1.0f });
		Redflag::Renderer2D::EndScene();
	}

}

void Sandbox2D::OnImGuiRender()
{
	RF_PROFILE_FUNCTION();

	// 第一个窗口：Settings
	ImGui::Begin("Settings");
	ImGui::ColorEdit4("SquareColor", glm::value_ptr(m_SquareColor));
	ImGui::End();
}

void Sandbox2D::OnEvent(Redflag::Event& event)
{
	m_CameraController.OnEvent(event);
}
