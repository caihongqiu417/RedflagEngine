#include "CharacterModel.h"
#include "Redflag/Animation/CharacterController.h"
#include "Redflag/Animation/Ragdoll.h"
#include "Redflag/ImGui/VulkanImGui.h"
#include "Platform/Vulkan/VulkanDraw.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>

using namespace Redflag;

namespace
{
	enum Mode { Forward, Reach, Move, Skin, Passive, Walk };
	const char* Names[] = {"正向运动学 FK", "逆向运动学 IK", "交互角色", "线性蒙皮", "布娃娃", "物理行走"};
	const Ragdoll::Entities Parts{11,12,13,14,15,16,17};

	Ref<SampledTexture> MakeColors(RenderDevice& device)
	{
		std::vector<glm::u8vec4> pixels;
		for (glm::vec3 tint : {glm::vec3(120,168,194),glm::vec3(228,168,100)})
			for (int i = 0; i < 16; ++i)
				pixels.emplace_back(glm::u8vec3(tint * (.50f + .50f*i/15.0f)),255);
		return device.CreateTexture(16,2,pixels.data());
	}

	glm::mat4 Box(glm::vec3 center, glm::quat rotation, glm::vec3 size)
	{
		return glm::scale(glm::translate(glm::mat4(1),center) * glm::mat4_cast(rotation),size);
	}

	class Demo
	{
	public:
		Demo(VulkanDraw& draw) : m_Draw(draw), m_Idle(AnimationClip::LoadBVH("assets/animations/GAMES105/Idle.bvh")),
			m_Walk(AnimationClip::LoadBVH("assets/animations/GAMES105/Walk.bvh")), m_Controller(m_Idle,m_Walk),
			m_Skin(MakeCharacterModel(m_Walk.Rig)), m_Colors(MakeColors(draw))
		{
			m_Cube = Mesh::CreateCube()->GetGeometry();
			m_World.AddBox({1,BodyType3D::Static,{0,-.2f,0},{1,0,0,0},{40,.2f,40}});
		}

		void Select(int mode)
		{
			m_Mode = mode;
			m_Ragdoll.reset();
			m_PhysicsTime = 0;
			if (mode == Passive || mode == Walk)
				m_Ragdoll = std::make_unique<Ragdoll>(m_World,Parts,glm::vec3(0,mode == Passive ? .65f : 0.0f,0));
		}

		void Controls()
		{
			for (int i = Forward; i <= Walk; ++i)
				if (ImGui::RadioButton(Names[i],m_Mode == i))
					Select(i);
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
			switch (m_Mode)
			{
			case Forward:
				ImGui::TextWrapped("转动左肩，观察下方的肘和手腕。");
				ImGui::SliderFloat("角度",&m_Angle,-100,100,"%.0f°");
				break;
			case Reach:
				ImGui::TextWrapped("金色方块是左手的目标。坐标相对左肩，单位为米。");
				ImGui::SliderFloat("左右",&m_Target.x,-.65f,.65f,"%.2f m");
				ImGui::SliderFloat("上下",&m_Target.y,-.65f,.65f,"%.2f m");
				ImGui::SliderFloat("前后",&m_Target.z,-.65f,.65f,"%.2f m");
				break;
			case Move:
				ImGui::TextWrapped("W、A、S、D 控制移动方向。松开后角色回到站立动作。");
				break;
			case Skin:
				ImGui::TextWrapped("拖动进度，查看同一张网格怎样随膝盖和手肘弯曲。");
				ImGui::SliderFloat("步态",&m_Time,0.0f,m_Walk.Duration(),"%.2f s");
				break;
			case Passive:
				ImGui::TextWrapped("放松关节，让下半身落在地面上。");
				if (ImGui::Button("重新落下")) Select(Passive);
				break;
			case Walk:
				ImGui::TextWrapped("髋、膝、踝带动两脚交替。用滑块调整向前的目标速度。");
				ImGui::SliderFloat("速度",&m_Speed,0.0f,1.1f,"%.2f m/s");
				if (ImGui::Button("重新站立")) Select(Walk);
				break;
			}
		}

		void Draw(float dt, glm::vec2 velocity, float aspect)
		{
			const Skeleton& rig = m_Walk.Rig;
			Pose local = rig.RestPose();
			local[0].Position.y = .91f;
			glm::vec3 focus(0,.85f,0), target(0);
			if (m_Mode == Forward)
				local[rig.Find("lShoulder")].Rotation = glm::angleAxis(glm::radians(m_Angle),glm::vec3(0,0,1));
			if (m_Mode == Reach)
			{
				glm::vec3 shoulder = rig.ToModelSpace(local)[rig.Find("lShoulder")].Position;
				target = shoulder + m_Target;
				rig.SolveTwoBone(local,rig.Find("lWrist"),target,shoulder+glm::vec3(.5f,-.3f,0));
			}
			if (m_Mode == Move)
			{
				local = m_Controller.Update(dt,velocity);
				focus += glm::vec3(local[0].Position.x,0,local[0].Position.z);
			}
			if (m_Mode == Skin)
			{
				local = m_Walk.Sample(m_Time);
				local[0].Position.x = local[0].Position.z = 0;
			}
			if (m_Ragdoll)
			{
				constexpr float step = 1.0f/60.0f;
				m_PhysicsTime += dt;
				while (m_PhysicsTime >= step)
				{
					if (m_Mode == Walk) m_Ragdoll->DriveWalk(step,m_Speed);
					m_World.Step(step);
					m_PhysicsTime -= step;
				}
				glm::vec3 p;
				glm::quat q;
				m_World.GetPose(Parts[Ragdoll::Pelvis],p,q);
				focus = {p.x,.7f,p.z};
			}
			const glm::vec3 camera = focus + glm::vec3(1.5f,.52f,2.7f);
			glm::mat4 vp = glm::perspective(glm::radians(39.0f),aspect,.03f,50.0f) * glm::lookAt(camera,focus,{0,1,0});
			auto drawBox = [&](glm::mat4 transform,glm::vec4 color,int id) {
				m_Draw.DrawIndexed(m_Cube,{},vp,transform,color,id);
			};
			Pose model = rig.ToModelSpace(local);
			Ref<MeshGeometry> surface;
			if (m_Mode == Move || m_Mode == Skin)
				surface = m_Draw.CreateMesh(m_Skin.Deform(model),m_Skin.GetIndices());

			m_Draw.BeginScene();
			const glm::quat identity(1,0,0,0);
			drawBox(Box({0,-.06f,0},identity,{80,.12f,80}),{.12f,.15f,.17f,1},1);
			for (int line = -8; line <= 8; ++line)
			{
				drawBox(Box({line*.5f,.001f,focus.z},identity,{.006f,.002f,14}),{.28f,.31f,.32f,1},1);
				drawBox(Box({0,.001f,std::floor(focus.z)+line*.5f},identity,{14,.002f,.006f}),{.28f,.31f,.32f,1},1);
			}
			if (m_Ragdoll)
			{
				for (int i = 0; i < Ragdoll::Count; ++i)
				{
					glm::vec3 p; glm::quat q;
					m_World.GetPose(Parts[i],p,q);
					glm::vec4 color = i == 0 ? glm::vec4(.83f,.62f,.38f,1) :
						i <= Ragdoll::LeftFoot ? glm::vec4(.28f,.57f,.72f,1) : glm::vec4(.57f,.76f,.82f,1);
					drawBox(Box(p,q,2.0f*Ragdoll::HalfSize(static_cast<Ragdoll::Limb>(i))),color,static_cast<int>(Parts[i]));
				}
			}
			else if (surface)
				m_Draw.DrawIndexed(surface,m_Colors,vp,glm::mat4(1),glm::vec4(1),2);
			else
			{
				for (size_t i=1; i<model.size(); ++i)
				{
					glm::vec3 a = model[rig.Joints[i].Parent].Position, b = model[i].Position;
					glm::vec3 delta = b-a;
					drawBox(Box((a+b)*.5f,glm::quat(glm::vec3(0,1,0),glm::normalize(delta)),
						{.026f,glm::length(delta),.026f}),{.58f,.76f,.84f,1},2);
					drawBox(Box(b,identity,{.042f,.042f,.042f}),{.92f,.68f,.39f,1},2);
				}
				if (m_Mode == Reach)
					drawBox(Box(target,identity,{.064f,.064f,.064f}),{1,.47f,.12f,1},3);
			}
			m_Draw.EndScene();
		}
	private:
		VulkanDraw& m_Draw;
		AnimationClip m_Idle, m_Walk;
		CharacterController m_Controller;
		SkinnedMesh m_Skin;
		Ref<SampledTexture> m_Colors;
		Ref<MeshGeometry> m_Cube;
		PhysicsWorld m_World;
		std::unique_ptr<Ragdoll> m_Ragdoll;
		int m_Mode = Forward;
		float m_Angle = -32.0f, m_Time = .27f, m_Speed = .8f, m_PhysicsTime = 0;
		glm::vec3 m_Target{.24f,.08f,.20f};
	};
}

int RunCharacterDemo(bool sequence)
{
	if (!glfwInit()) throw std::runtime_error("GLFW init failed");
	glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API);
	std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window(
		glfwCreateWindow(1050,650,"Redflag | Character Animation",nullptr,nullptr),glfwDestroyWindow);
	if (!window) throw std::runtime_error("Character window unavailable");
	VulkanContext context(window.get());
	context.Init();
	VulkanDraw renderer(context);
	RenderDevice::SetCurrent(&renderer);
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/msyh.ttc",18,nullptr,io.Fonts->GetGlyphRangesChineseFull());
	ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();
	style.Colors[ImGuiCol_WindowBg] = {0.14f,0.14f,0.15f,1};
	style.Colors[ImGuiCol_FrameBg] = {.24f,.23f,.24f,1};
	style.Colors[ImGuiCol_CheckMark] = style.Colors[ImGuiCol_SliderGrab] = {.88f,.43f,.40f,1};
	style.Colors[ImGuiCol_FrameBgActive] = style.Colors[ImGuiCol_FrameBgHovered] = {.45f,.21f,.21f,1};
	ImGui_ImplGlfw_InitForVulkan(window.get(),true);
	{
		VulkanImGui ui(context);
		Demo demo(renderer);
		VkDescriptorSet image = VK_NULL_HANDLE;
		double previous = glfwGetTime();
		int frame = 0;
		while (!glfwWindowShouldClose(window.get()))
		{
			glfwPollEvents();
			if (glfwGetKey(window.get(),GLFW_KEY_ESCAPE) == GLFW_PRESS) break;
			if (!context.BeginFrame()) continue;
			double now = glfwGetTime();
			float dt = sequence ? 1.0f/60 : static_cast<float>(now - previous);
			previous = now;
			if (sequence && frame % 60 == 0)
			{
				int mode = frame/60;
				demo.Select(mode);
				glfwSetWindowTitle(window.get(),Names[mode]);
			}
			ui.NewFrame();
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();
			for (int i = Forward; i <= Walk; ++i)
				if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_1 + i),false))
					demo.Select(i);
			ImGui::SetNextWindowPos({0,0});
			ImGui::SetNextWindowSize(io.DisplaySize);
			ImGui::Begin("角色动画",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
			ImGui::BeginChild("选择动作",{265,0},true);
			ImGui::TextUnformatted("角色动画");
			ImGui::TextDisabled("数字键 1–6 切换动作");
			ImGui::Separator();
			demo.Controls();
			ImGui::EndChild();
			ImGui::SameLine();
			ImVec2 size = ImGui::GetContentRegionAvail();
			if (size.x > 1 && size.y > 1)
			{
				uint32_t w = static_cast<uint32_t>(size.x * io.DisplayFramebufferScale.x);
				uint32_t h = static_cast<uint32_t>(size.y * io.DisplayFramebufferScale.y);
				if (renderer.Resize(w,h))
				{
					if (image) ImGui_ImplVulkan_RemoveTexture(image);
					image = ImGui_ImplVulkan_AddTexture(renderer.GetSampler(),renderer.GetColorView(),VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				}
				glm::vec2 velocity(float(glfwGetKey(window.get(),GLFW_KEY_D)-glfwGetKey(window.get(),GLFW_KEY_A)),
					float(glfwGetKey(window.get(),GLFW_KEY_W)-glfwGetKey(window.get(),GLFW_KEY_S)));
				if (glm::length(velocity) > 0.0f) velocity = glm::normalize(velocity);
				if (sequence) velocity = {.65f,.45f};
				demo.Draw(dt,velocity,float(w)/h);
				ImGui::Image((ImTextureID)image,size);
			}
			ImGui::End();
			ImGui::Render();
			ui.Render();
			context.SwapBuffers();
			if (sequence && ++frame == 360) break;
		}
		context.WaitIdle();
		if (image) ImGui_ImplVulkan_RemoveTexture(image);
	}
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	RenderDevice::SetCurrent(nullptr);
	return 0;
}
