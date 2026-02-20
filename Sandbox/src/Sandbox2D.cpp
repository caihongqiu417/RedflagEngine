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
	RF_PROFILE_FUNCTION();

	m_RedCloudTexture = Redflag::Texture2D::Create("assets/textures/RedCloud.png");
	m_CheckerboardTexture = Redflag::Texture2D::Create("assets/textures/Checkerboard.png");
}

void Sandbox2D::OnDetach()
{
	RF_PROFILE_FUNCTION();
}

void Sandbox2D::OnUpdate(Redflag::Timestep ts)
{
	RF_PROFILE_FUNCTION();
    
	//updata camera
	m_CameraController.OnUpdate(ts);

	{
		//Render
		RF_PROFILE_SCOPE("Renderer Preparation");
		Redflag::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1 });
		Redflag::RenderCommand::Clear();
	}
   
	{
		static float rotation = 0;
		rotation += 50 * ts;  // 每秒旋转50度
		//Render scene
		RF_PROFILE_SCOPE("Renderer Draw");
		Redflag::Renderer2D::BeginScene(m_CameraController.GetCamera());

		// 负的值越大，渲染的越早（越靠后）
		Redflag::Renderer2D::DrawQuad({ 0.4f, 0.4f, -0.7f }, { 1.5f, 1.5f }, { 0.8f,0.4f,0.2f,1.0f });
		Redflag::Renderer2D::DrawRotatedQuad({ -0.4f, -0.4f, -0.6f }, { 1.5f, 1.5f }, -15.f, { 0.2f,0.3f,0.8f,1.0f });
		Redflag::Renderer2D::DrawQuad({ 0.0f, 0.0f, -0.8f }, { 3.0f, 3.0f }, m_CheckerboardTexture, 3.0f);
		Redflag::Renderer2D::DrawQuad({ 0.0f, 0.0f, -0.5f }, { 2.0f, 2.0f }, m_RedCloudTexture, 1.0f);
		Redflag::Renderer2D::DrawRotatedQuad({ 0.4f, 0.4f, -0.2f }, { 1.0f, 1.0f }, rotation, m_CheckerboardTexture, 1.0f);

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
