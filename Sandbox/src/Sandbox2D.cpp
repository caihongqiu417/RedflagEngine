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

    // 更新相机
    m_CameraController.OnUpdate(ts);

    // 重置统计数据
    Redflag::Renderer2D::ResetStats();

    // 清屏
    {
        RF_PROFILE_SCOPE("Renderer Preparation");
        Redflag::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1 });
        Redflag::RenderCommand::Clear();
    }

    //=========================================================================
    // 第一批次：绘制背景网格
    //=========================================================================
    {
        RF_PROFILE_SCOPE("Renderer Draw - Background Grid");
        Redflag::Renderer2D::BeginScene(m_CameraController.GetCamera());

        float range = 8.0f;       // 范围 ±8
        float step = 0.8f;         // 步长
        float gridSize = 0.3f;      // 格子大小
        float gridZ = -0.9f;        // 放在最背面

        for (float y = -range; y <= range; y += step)
        {
            for (float x = -range; x <= range; x += step)
            {
                // 简单的蓝绿色网格
                glm::vec4 color = { 0.3f, 0.6f, 0.8f, 0.5f };  // 半透明蓝绿色
                Redflag::Renderer2D::DrawQuad({ x, y, gridZ }, { gridSize, gridSize }, color);
            }
        }

        Redflag::Renderer2D::EndScene();
    }

    //=========================================================================
    // 第二批次：绘制前景纹理和半透明正方形
    //=========================================================================
    {
        static float rotation = 0;
        rotation += 30.0f * ts;  // 慢速旋转

        RF_PROFILE_SCOPE("Renderer Draw - Textures and Transparent Squares");
        Redflag::Renderer2D::BeginScene(m_CameraController.GetCamera());

        // 两个纹理方块（不透明或半透明）
        Redflag::Renderer2D::DrawQuad({ -2.5f, 1.8f, -0.6f }, { 2.0f, 2.0f }, m_CheckerboardTexture, 2.0f, { 1.0f, 1.0f, 1.0f, 0.9f });
        Redflag::Renderer2D::DrawQuad({ 2.5f, -1.8f, -0.6f }, { 2.0f, 2.0f }, m_RedCloudTexture, 1.5f, { 1.0f, 0.9f, 0.9f, 0.8f });

        // 几个半透明的纯色正方形，放在网格前面
        Redflag::Renderer2D::DrawQuad({ -1.5f, 0.5f, -0.5f }, { 1.5f, 1.5f }, { 1.0f, 0.2f, 0.2f, 0.6f });  // 半透明红
        Redflag::Renderer2D::DrawQuad({ 1.5f, -0.5f, -0.5f }, { 1.5f, 1.5f }, { 0.2f, 1.0f, 0.2f, 0.6f });  // 半透明绿
        Redflag::Renderer2D::DrawQuad({ -1.0f, -1.8f, -0.4f }, { 1.5f, 1.5f }, { 0.2f, 0.4f, 1.0f, 0.7f }); // 半透明蓝
        Redflag::Renderer2D::DrawRotatedQuad({ 1.0f, 1.8f, -0.3f }, { 1.5f, 1.5f }, rotation, { 1.0f, 0.8f, 0.2f, 0.5f }); // 半透明黄（旋转）

        Redflag::Renderer2D::EndScene();
    }
}

void Sandbox2D::OnImGuiRender()
{
	RF_PROFILE_FUNCTION();

	// 第一个窗口：Settings
	ImGui::Begin("Settings");

	auto stats=Redflag::Renderer2D::GetStats();
	ImGui::Text("Renderer2D Stats:");
	ImGui::Text("Draw Calls: %d", stats.DrawCalls);
	ImGui::Text("Quads: %d", stats.QuadCount);
	ImGui::Text("Vertices: %d", stats.GetTotalVertexCount());
	ImGui::Text("Indices: %d", stats.GetTotalIndexCount());

	ImGui::ColorEdit4("SquareColor", glm::value_ptr(m_SquareColor));
	ImGui::End();
}

void Sandbox2D::OnEvent(Redflag::Event& event)
{
	m_CameraController.OnEvent(event);
}
