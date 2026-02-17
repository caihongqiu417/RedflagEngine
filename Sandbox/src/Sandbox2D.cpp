#include "rfpch.h"

#include "Redflag.h"


#include "Platform/OpenGL/OpenGLShader.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "Redflag/Renderer/Shader.h"

#include "Sandbox2D.h"

//计时器，栈对象，离开作用域自动销毁
//可以手动调用Stop()提前结束计时，析构函数会检查是否已经停止，如果没有则自动调用Stop()
template<typename Fn>
class Timer
{
public:
	Timer(const char* name,Fn&& func)
		:m_Name(name), m_Func(std::forward<Fn>(func)),m_Stopped(false)
	{
		m_StartTimePoint = std::chrono::high_resolution_clock::now();
	}

	void Stop()
	{
		auto endTimePoint = std::chrono::high_resolution_clock::now();
		auto start = std::chrono::time_point_cast<std::chrono::microseconds>(m_StartTimePoint).time_since_epoch().count();
		auto end = std::chrono::time_point_cast<std::chrono::microseconds>(endTimePoint).time_since_epoch().count();
		float duration = (end - start) * 0.001f;
		m_Stopped = true;

		// 记录结果,传递给外部函数,
		m_Func({ m_Name,duration });
	}

	~Timer()
	{
		if (!m_Stopped)
			Stop();
	}

private:
	const char* m_Name;
	std::chrono::time_point<std::chrono::high_resolution_clock> m_StartTimePoint;
	bool m_Stopped;
	Fn m_Func;
};

#define PROFILE_SCOPE(name) Timer timer##__LINE__(name, [this](ProfileResult profileResult){ m_ProfileResults.push_back(profileResult); });


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
	PROFILE_SCOPE("Overall:Redflag Sandbox2D::OnUpdate");
    
	{
		//updata camera
		PROFILE_SCOPE("CameraController::OnUpdate");
		m_CameraController.OnUpdate(ts);
	}

	{
		//Render
		PROFILE_SCOPE("RendererCommand::Clear");
		Redflag::RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1 });
		Redflag::RenderCommand::Clear();
	}
   
	{
		//Render scene
		PROFILE_SCOPE("Renderer2D::Draw");
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
	// 第一个窗口：Settings
	ImGui::Begin("Settings");
	ImGui::ColorEdit4("SquareColor", glm::value_ptr(m_SquareColor));
	ImGui::End();

	// 第二个窗口：Profile
	ImGui::Begin("Profile");
	for (auto& result : m_ProfileResults)
	{
		ImGui::Text("%s: %.5fms", result.Name.c_str(), result.Time);
	}
	m_ProfileResults.clear();
	ImGui::End();
}

void Sandbox2D::OnEvent(Redflag::Event& event)
{
	m_CameraController.OnEvent(event);
}
