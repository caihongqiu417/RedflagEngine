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

	// 加载纹理资源
	m_RedCloudTexture = Redflag::Texture2D::Create("assets/textures/RedCloud.png");
	m_CheckerboardTexture = Redflag::Texture2D::Create("assets/textures/Checkerboard.png");

   // 粒子系统属性初始化
    m_Particle.ColorBegin = { 0.4f, 0.7f, 1.0f, 1.0f };  // 淡蓝色开始
    m_Particle.ColorEnd = { 1.0f, 0.5f, 0.8f, 0.0f };    // 粉红色渐隐

    m_Particle.SizeBegin = 0.1f;      // 很小
    m_Particle.SizeEnd = 0.02f;        // 更小
    m_Particle.SizeVariation = 0.03f;  // 微小变化

    m_Particle.Velocity = { 0.0f, 0.0f };           // 基础速度为0
    m_Particle.VelocityVariation = { 2.0f, 2.0f };  // 大范围变化，向四面八方散开

    m_Particle.LifeTime = 1.8f;  // 存活时间
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
    // 第一批次：绘制背景网格（简单渐变）
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
                // 计算距离中心的距离（归一化到0-1）
                float dist = sqrt(x * x + y * y) / range;

                // 根据距离渐变：中心亮蓝，边缘暗蓝
                glm::vec4 color = {
                    0.2f,                    // R 固定低值
                    0.3f + 0.5f * (1 - dist), // G 从中心0.8到边缘0.3
                    0.8f * (1 - dist * 0.5f), // B 从中心0.8到边缘0.4
                    0.6f                      // Alpha 固定
                };

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
        Redflag::Renderer2D::DrawRotatedQuad({ 1.0f, 1.8f, -0.3f }, { 1.5f, 1.5f }, glm::radians(rotation), {1.0f, 0.8f, 0.2f, 0.5f}); // 半透明黄（旋转）

        Redflag::Renderer2D::EndScene();
    }

    //=========================================================================
    // 粒子系统：鼠标点击生成粒子效果
    //=========================================================================
    if (Redflag::Input::IsMouseButtonPressed(RF_MOUSE_BUTTON_LEFT))
    {
        // 1. 获取鼠标在屏幕上的像素坐标
        auto [mouseX, mouseY] = Redflag::Input::GetMousePosition();

        // 2. 获取窗口大小（像素单位）
        auto windowWidth = Redflag::Application::Get().GetWindow().GetWidth();
        auto windowHeight = Redflag::Application::Get().GetWindow().GetHeight();

        // 3. 获取相机信息（用于坐标转换）
        auto cameraBounds = m_CameraController.GetBounds();  // 相机能看到的世界范围
        auto cameraPos = m_CameraController.GetCamera().GetPosition();   // 相机在世界中的位置

        // 4. 将鼠标从屏幕坐标转换为世界坐标
        //    屏幕坐标(0~窗口大小) -> 世界坐标(相机范围内的实际位置)
        float worldX = (mouseX / windowWidth) * cameraBounds.GetWidth() - cameraBounds.GetWidth() * 0.5f;
        float worldY = cameraBounds.GetHeight() * 0.5f - (mouseY / windowHeight) * cameraBounds.GetHeight();

        // 5. 设置粒子发射位置（加上相机偏移）
        m_Particle.Position = { worldX + cameraPos.x, worldY + cameraPos.y };


        // 先清空所有粒子再发射
        //m_ParticleSystem.Clear();  // 添加这行！

        // 6. 每次点击发射粒子，产生喷射效果
        for (int i = 0; i < 5; i++)
            m_ParticleSystem.Emit(m_Particle);
    }

    // 更新所有粒子的状态（位置、生命周期、透明度等）
    m_ParticleSystem.OnUpdate(ts);

    // 渲染所有粒子
    m_ParticleSystem.OnRender(m_CameraController.GetCamera());
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
