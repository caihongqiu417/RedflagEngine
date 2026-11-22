#include "rfpch.h"
#include "DecisionPanel.h"
#include "Redflag/Scene/Scene.h"
#include "Redflag/Scene/Components.h"
#include <imgui.h>

namespace Redflag
{
	void DecisionPanel::Draw(const Ref<Scene>& scene)
	{
		if (m_Scene.lock() != scene) {
			m_Client.Clear();
			m_Scene = scene;
			m_Pending = 0;
			m_Message.clear();
		}
		if (m_Pending) {
			std::optional<DecisionResult> result = m_Client.Take(m_Pending);
			if (result) {
				m_Pending = 0;
				if (result->Status == DecisionStatus::Completed)
					m_Message = result->Text.empty() ? "选择了 " + result->Choice : result->Text;
				else if (result->Status == DecisionStatus::Cancelled) m_Message = "已取消";
				else m_Message = "请求失败：" + result->Error;
			}
		}

		if (!ImGui::BeginMenu("模型测试")) return;
		ImGui::InputTextWithHint("模型", "留空使用本机服务的默认模型", m_Model, sizeof(m_Model));
		ImGui::InputTextWithHint("问题", "例如：为维修站的备用钥匙写一句物品说明", m_Prompt, sizeof(m_Prompt));
		ImGui::Checkbox("从场景物体中选择", &m_PickObject);
		ImGui::BeginDisabled((m_PickObject && !scene) || !m_Prompt[0] || m_Pending != 0);
		if (ImGui::Button("发送问题")) {
			DecisionRequest request;
			request.Prompt = m_Prompt;
			request.Model = m_Model;
			m_Message.clear();
			if (m_PickObject) {
				scene->GetRegistry().view<IDComponent,TagComponent>().each([&](const IDComponent& id, const TagComponent& tag) {
					request.Candidates.push_back({ std::to_string(uint64_t(id.ID)), tag.Tag });
				});
				request.Candidates.push_back({ "none", "没有合适的物体" });
				m_Pending = m_Client.Start(std::move(request));
			} else m_Pending = m_Client.StartReply(std::move(request));
		}
		ImGui::EndDisabled();
		if (m_Pending) {
			ImGui::SameLine();
			if (ImGui::Button("取消")) m_Client.Cancel(m_Pending);
			ImGui::TextUnformatted("正在等待模型");
		} else if (!m_Message.empty()) {
			ImGui::TextWrapped("%s", m_Message.c_str());
			if (ImGui::Button("复制结果")) ImGui::SetClipboardText(m_Message.c_str());
		}
		ImGui::EndMenu();
	}
}
