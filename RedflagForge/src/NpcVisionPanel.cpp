#include "rfpch.h"
#include "NpcVisionPanel.h"
#include "Redflag/Renderer/EditorCamera.h"
#include "Redflag/Scene/Scene.h"
#include "Redflag/Scene/Entity.h"
#include <imgui.h>

namespace Redflag
{
	bool NpcVisionPanel::Draw(const Ref<Scene>& scene, const std::filesystem::path& file, bool simulating, EditorCamera& camera)
	{
		// 等待模型回答时，编辑器仍能移动相机、推进物理。收到答案后再读取场景里现在的物体。
		if (m_Answer.valid() && m_Answer.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
		{
			try
			{
				NpcVisionCandidate answer = m_Answer.get();
				if (m_QuestionScene.lock() == scene)
				{
					m_Region = std::move(answer.Region);
					Entity item = answer.Entity ? scene->GetEntityByUUID(*answer.Entity) : Entity{};
					if (m_Region) m_Message = m_Region->Camera + " 拍到了「" + m_Region->Label + "」。";
					else if (!answer.Entity) m_Message = "图片里没有找到相符的物体或区域。";
					else if (!item) m_Message = "图片里的物体已离开场景。";
					else if (!simulating) m_Message = item.GetName() + "：启动物理模拟后可查询拿取距离与遮挡。";
					else m_Message = item.GetName() + (CanPickUp(*scene, *answer.Entity, camera.GetPosition(), m_Reach)
						? "：当前位置可以拿取。" : "：当前位置无法拿取。");
				}
			}
			catch (const std::exception& error)
			{
				m_Message = std::string("视觉服务未能完成查询。") + error.what();
			}
		}
		if (!ImGui::BeginMenu("NPC 观察")) return false;
		bool showRegion = false;
		ImGui::TextUnformatted("以编辑相机的位置试拿物体。");
		ImGui::InputText("问题", m_Question, sizeof(m_Question));
		ImGui::DragFloat("可达距离（米）", &m_Reach, .1f, .1f, 20.f, "%.1f");
		ImGui::BeginDisabled(file.empty() || !m_Question[0] || m_Answer.valid());
		if (ImGui::Button("查询图片"))
		{
			m_QuestionScene = scene;
			m_Message.clear();
			m_Region.reset();
			m_Answer = AskNpcCandidate(file.stem().u8string(), m_Question);
		}
		ImGui::EndDisabled();
		if (m_Answer.valid()) ImGui::TextUnformatted("等待视觉模型回答…");
		else if (!m_Message.empty() && m_QuestionScene.lock() == scene)
		{
			ImGui::TextWrapped("%s", m_Message.c_str());
			if (m_Region && ImGui::Button("从拍摄机位看向该处"))
			{
				camera.LookAt(m_Region->Origin, m_Region->Origin + m_Region->Direction);
				showRegion = true;
			}
		}
		ImGui::EndMenu();
		return showRegion;
	}
}
