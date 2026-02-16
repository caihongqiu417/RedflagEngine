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
    
}

void Sandbox2D::OnDetach()
{
}

void Sandbox2D::OnUpdate(Redflag::Timestep ts)
{
    //Update
	m_CameraController.OnUpdate(ts);


    //Render
    Redflag::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1 });
	Redflag::RenderCommand::Clear();

	Redflag::Renderer2D::BeginScene(m_CameraController.GetCamera());
	Redflag::Renderer2D::DrawQuad({ -1.0f, 0.0f }, { 0.8f, 0.8f }, m_SquareColor);
	Redflag::Renderer2D::EndScene();

}

void Sandbox2D::OnImGuiRender()
{
	ImGui::Begin("Settings");
	ImGui::ColorEdit4("SquareColor", glm::value_ptr(m_SquareColor));
	ImGui::End();
}

void Sandbox2D::OnEvent(Redflag::Event& event)
{
	m_CameraController.OnEvent(event);
}
