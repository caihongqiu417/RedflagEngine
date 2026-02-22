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

	// ============== 加载纹理资源 =========================
	m_RedCloudTexture = Redflag::Texture2D::Create("assets/textures/RedCloud.png");
	m_CheckerboardTexture = Redflag::Texture2D::Create("assets/textures/Checkerboard.png");
	m_SpriteSheet = Redflag::Texture2D::Create("assets/game/textures/RPGpack_sheet_2X.png");
	m_rogueLikeCharSpriteSheet = Redflag::Texture2D::Create("assets/game/textures/roguelikeChar_transparent.png");

	// ============== 从精灵表中创建子纹理 =========================
    m_TextureStairs = Redflag::SubTexture2D::CreateFromCoords(m_SpriteSheet, { 7, 6 }, { 128, 128 }, { 1, 1 });
    m_TextureBarrel = Redflag::SubTexture2D::CreateFromCoords(m_SpriteSheet, { 8, 2 }, { 128, 128 }, { 1, 1 });
    m_TextureTree = Redflag::SubTexture2D::CreateFromCoords(m_SpriteSheet, { 2, 1 }, { 128, 128 }, { 1, 2 });

    m_TextureRogueLikeCharFrame1 = Redflag::SubTexture2D::CreateFromCoords(m_rogueLikeCharSpriteSheet, { 0, 0 }, { 16, 16 }, { 1, 1 });
    m_TextureRogueLikeCharFrame2 = Redflag::SubTexture2D::CreateFromCoords(m_rogueLikeCharSpriteSheet, { 1, 0 }, { 16, 16 }, { 1, 1 });

	// =============== 配置粒子属性 =========================
    m_Particle.ColorBegin = { 0.4f, 0.7f, 1.0f, 1.0f };  // 淡蓝色开始
    m_Particle.ColorEnd = { 1.0f, 0.5f, 0.8f, 0.0f };    // 粉红色渐隐

    m_Particle.SizeBegin = 0.1f;      // 很小
    m_Particle.SizeEnd = 0.02f;        // 更小
    m_Particle.SizeVariation = 0.03f;  // 微小变化

    m_Particle.Velocity = { 0.0f, 0.0f };           // 基础速度为0
    m_Particle.VelocityVariation = { 2.0f, 2.0f };  // 大范围变化，向四面八方散开

    m_Particle.LifeTime = 1.8f;  // 存活时间

    //============== 配置相机初始位置 =========================
	m_CameraController.SetZoomLevel(6.f); // 初始缩放级别
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
    // 第一批次：绘制背景网格（暗蓝黑渐变 + 丰富细节）
    //=========================================================================
    {
        RF_PROFILE_SCOPE("Renderer Draw - Background Grid");
        Redflag::Renderer2D::BeginScene(m_CameraController.GetCamera());

        float range = 12.0f;       // 范围 ±12
        float step = 0.8f;         // 步长
        float gridSize = 0.4f;      // 格子大小
        float gridZ = -0.9f;        // 放在最背面

        for (float y = -range; y <= range; y += step)
        {
            for (float x = -range; x <= range; x += step)
            {
                // 计算距离中心的距离（归一化到0-1）
                float dist = sqrt(x * x + y * y) / range;

                // 计算角度，增加细节变化
                float angle = atan2(y, x) / (2.0f * 3.14159f) + 0.5f;

                // 暗蓝黑色调 + 丰富细节
                glm::vec4 color = {
                    0.15f + 0.1f * sin(x * 0.5f) + 0.05f * cos(angle * 8.0f),     // 暗红 (0.05-0.3)
                    0.2f + 0.1f * cos(y * 0.4f) + 0.05f * sin(dist * 12.0f),      // 暗绿 (0.1-0.35)
                    0.3f + 0.15f * sin((x + y) * 0.4f) + 0.05f * cos(angle * 6.0f), // 暗蓝 (0.15-0.5)
                    0.5f                                                          // 半透明
                };

                Redflag::Renderer2D::DrawQuad({ x, y, gridZ }, { gridSize, gridSize }, color);
            }
        }

        Redflag::Renderer2D::EndScene();
    }

    //=========================================================================
  // 第二批次：绘制前景纹理和半透明正方形（放在左侧）
  //=========================================================================
    {
        static float rotation = 0;
        rotation += 30.0f * ts;

        RF_PROFILE_SCOPE("Renderer Draw - Textures and Transparent Squares");
        Redflag::Renderer2D::BeginScene(m_CameraController.GetCamera());

        // 所有非子纹理绘制在左侧区域（x <= 0）
        Redflag::Renderer2D::DrawQuad({ -2.5f, 1.8f, -0.6f }, { 4.0f, 4.0f }, m_CheckerboardTexture, 2.0f, { 1.0f, 1.0f, 1.0f, 0.9f });
        Redflag::Renderer2D::DrawQuad({ 2.5f, -1.8f, -0.6f }, { 4.0f, 4.0f }, m_RedCloudTexture, 1.5f, { 1.0f, 0.9f, 0.9f, 1.0f });
        Redflag::Renderer2D::DrawQuad({ -1.5f, 0.5f, -0.5f }, { 1.5f, 1.5f }, { 1.0f, 0.2f, 0.2f, 0.6f });
        Redflag::Renderer2D::DrawQuad({ 1.5f, -0.5f, -0.5f }, { 1.5f, 1.5f }, { 0.2f, 1.0f, 0.2f, 0.6f });
        Redflag::Renderer2D::DrawQuad({ -1.0f, -1.8f, -0.4f }, { 1.5f, 1.5f }, { 0.2f, 0.4f, 1.0f, 0.7f });
        Redflag::Renderer2D::DrawRotatedQuad({ 1.0f, 1.8f, -0.3f }, { 1.5f, 1.5f }, glm::radians(rotation), { 1.0f, 0.8f, 0.2f, 0.5f });


        Redflag::Renderer2D::EndScene();
    }

    //=========================================================================
    // 第三批次：绘制子纹理（放在右侧区域，x > 0）
    //=========================================================================
    {
        RF_PROFILE_SCOPE("Renderer Draw - SubTextures");
        Redflag::Renderer2D::BeginScene(m_CameraController.GetCamera());

        // 所有子纹理绘制在右侧区域（x > 0），Z轴设为0.1f确保在最前面
        Redflag::Renderer2D::DrawQuad({ 4.0f, 2.5f, 0.1f }, { 1.5f, 1.5f }, m_TextureStairs, 1.0f, { 1.0f, 1.0f, 1.0f, 1.0f });
        Redflag::Renderer2D::DrawQuad({ 6.0f, 2.5f, 0.1f }, { 1.2f, 1.2f }, m_TextureBarrel, 1.0f, { 1.0f, 0.9f, 0.8f, 1.0f });
        Redflag::Renderer2D::DrawQuad({ 8.0f, 2.5f, 0.1f }, { 1.5f, 3.0f }, m_TextureTree, 1.0f, { 0.9f, 1.0f, 0.9f, 1.0f });

        // 第二行子纹理（带旋转）
        Redflag::Renderer2D::DrawRotatedQuad({ 4.0f, -1.0f, 0.1f }, { 1.5f, 1.5f }, glm::radians(15.0f), m_TextureStairs, 1.0f, { 0.8f, 0.8f, 1.0f, 0.8f });
        Redflag::Renderer2D::DrawRotatedQuad({ 6.0f, -1.0f, 0.1f }, { 1.2f, 1.2f }, glm::radians(30.0f), m_TextureBarrel, 1.0f, { 1.0f, 0.8f, 0.8f, 0.8f });
        Redflag::Renderer2D::DrawRotatedQuad({ 8.0f, -1.0f, 0.1f }, { 1.5f, 3.0f }, glm::radians(45.0f), m_TextureTree, 1.0f, { 0.8f, 1.0f, 0.8f, 0.8f });

        Redflag::Renderer2D::EndScene();
    }

    //=========================================================================
    // 第四批次：两个角色不同移动轨迹和变换角度
    //=========================================================================
    {
        RF_PROFILE_SCOPE("Renderer Draw - Two Moving Characters");
        Redflag::Renderer2D::BeginScene(m_CameraController.GetCamera());

        // 帧计数器
        static int frameCounter = 0;
        static int animFrame1 = 0;
        static int animFrame2 = 0;

        // 角色1移动
        static float moveOffset1 = 0.0f;
        static float moveDirection1 = 1.0f;

        // 角色2移动
        static float moveOffset2 = 0.0f;
        static float moveDirection2 = 1.0f;

        frameCounter++;

        // 角色1：水平移动
        moveOffset1 += 0.02f * moveDirection1 * 60.0f * ts;
        if (moveOffset1 > 5.0f) moveDirection1 = -1.0f;
        if (moveOffset1 < -5.0f) moveDirection1 = 1.0f;

        // 角色2：垂直移动
        moveOffset2 += 0.03f * moveDirection2 * 60.0f * ts;
        if (moveOffset2 > 3.0f) moveDirection2 = -1.0f;
        if (moveOffset2 < -3.0f) moveDirection2 = 1.0f;

        // 每120帧切换一次纹理（角色1）
        if (frameCounter >= 120)
        {
            frameCounter = 0;
            animFrame1 = (animFrame1 + 1) % 2;
        }

        // 每60帧切换一次纹理（角色2，不同步）
        static int frameCounter2 = 0;
        frameCounter2++;
        if (frameCounter2 >= 60)
        {
            frameCounter2 = 0;
            animFrame2 = (animFrame2 + 1) % 2;
        }

        // 角色1：水平移动，旋转15度
        float posX1 = -3.0f + moveOffset1;
        float posY1 = 1.0f;

        // 角色2：垂直移动，旋转30度
        float posX2 = 3.0f;
        float posY2 = 0.0f + moveOffset2;

        // 绘制角色1
        if (animFrame1 == 0)
        {
            Redflag::Renderer2D::DrawRotatedQuad({ posX1, posY1, 0.1f }, { 1.5f, 1.5f }, glm::radians(15.0f), m_TextureRogueLikeCharFrame1, 1.0f, { 1.0f, 1.0f, 1.0f, 1.0f });
        }
        else
        {
            Redflag::Renderer2D::DrawRotatedQuad({ posX1, posY1, 0.1f }, { 1.5f, 1.5f }, glm::radians(15.0f), m_TextureRogueLikeCharFrame2, 1.0f, { 1.0f, 1.0f, 1.0f, 1.0f });
        }

        // 绘制角色2
        if (animFrame2 == 0)
        {
            Redflag::Renderer2D::DrawQuad({ posX2, posY2, 0.1f }, { 1.5f, 1.5f },  m_TextureRogueLikeCharFrame1, 1.0f, { 0.9f, 0.9f, 1.0f, 0.9f });
        }
        else
        {
            Redflag::Renderer2D::DrawQuad({ posX2, posY2, 0.1f }, { 1.5f, 1.5f },  m_TextureRogueLikeCharFrame2, 1.0f, { 0.9f, 0.9f, 1.0f, 0.9f });
        }

        Redflag::Renderer2D::EndScene();
    }

    //=========================================================================
	// 粒子系统：鼠标点击生成粒子效果（OnRender内部BeginScene/EndScene包裹）
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

	// 第一个窗口：Profile
	ImGui::Begin("Profile");

	auto stats=Redflag::Renderer2D::GetStats();
	ImGui::Text("Renderer2D Stats:");
	ImGui::Text("Draw Calls: %d", stats.DrawCalls);
	ImGui::Text("Quads: %d", stats.QuadCount);
	ImGui::Text("Vertices: %d", stats.GetTotalVertexCount());
	ImGui::Text("Indices: %d", stats.GetTotalIndexCount());

	ImGui::End();

    //第二个窗口：Color Editor
	ImGui::Begin("Settings");
	ImGui::ColorEdit4("SquareColor", glm::value_ptr(m_SquareColor));
	ImGui::End();
}

void Sandbox2D::OnEvent(Redflag::Event& event)
{
	m_CameraController.OnEvent(event);
}
