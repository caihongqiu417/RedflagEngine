#include "rfpch.h"
#include "NpcVisionPanel.h"
#include "Redflag/Scene/Scene.h"
#include "Redflag/Scene/Entity.h"
#include <imgui.h>

namespace Redflag
{
	void NpcVisionPanel::Draw(const Ref<Scene>& scene, const std::filesystem::path& file, bool simulating, glm::vec3 eye)
	{
		// 等待模型回答时，编辑器仍能移动相机、推进物理。收到答案后再读取场景里现在的物体。
		if (m_Answer.valid() && m_Answer.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
		{
			try
			{
				const std::optional<uint64_t> id = m_Answer.get();
				if (m_QuestionScene.lock() == scene)
				{
					Entity item = id ? scene->GetEntityByUUID(*id) : Entity{};
					if (!id) m_Message = "图片里没有选出物体。";
					else if (!item) m_Message = "图片里的物体已离开场景。";
					else m_Message = item.GetName() + (CanPickUp(*scene, *id, eye, m_Reach)
						? "：当前位置可以拿取。" : "：当前位置无法拿取。");
				}
			}
			catch (const std::exception& error)
			{
				m_Message = std::string("视觉服务未能完成查询。") + error.what();
			}
		}
		if (!ImGui::BeginMenu("NPC 观察")) return;
		ImGui::TextUnformatted("以编辑相机的位置试拿物体。");
		ImGui::InputText("问题", m_Question, sizeof(m_Question));
		ImGui::DragFloat("可达距离（米）", &m_Reach, .1f, .1f, 20.f, "%.1f");
		ImGui::BeginDisabled(!simulating || file.empty() || !m_Question[0] || m_Answer.valid());
		if (ImGui::Button("查询图片"))
		{
			m_QuestionScene = scene;
			m_Message.clear();
			m_Answer = AskNpcVision(file.stem().u8string(), m_Question);
		}
		ImGui::EndDisabled();
		if (!simulating) ImGui::TextDisabled("先点“模拟物理”，让物体参与碰撞查询。");
		else if (m_Answer.valid()) ImGui::TextUnformatted("等待视觉模型回答…");
		else if (!m_Message.empty() && m_QuestionScene.lock() == scene) ImGui::TextWrapped("%s", m_Message.c_str());
		ImGui::EndMenu();
	}
}
