#include "rfpch.h"
#include "NpcDecisionView.h"
#include "Redflag/AI/NpcSystem.h"
#include <imgui.h>

namespace Redflag
{
	static const NpcDecisionTrace* Selected(const NpcState* state, uint64_t id)
	{
		if (!state || state->Decisions.empty()) return nullptr;
		for (const NpcDecisionTrace& trace : state->Decisions)
			if (trace.ID == id) return &trace;
		return &state->Decisions.back();
	}

	static std::string Label(const NpcDecisionTrace& trace)
	{
		return "#" + std::to_string(trace.ID) + "  " + trace.Kind + "  ·  " + trace.Status;
	}

	//========== 请求、回复与本轮候选 ==========

	static void DrawRaw(const char* title, const std::string& raw)
	{
		ImGui::PushID(title);
		ImGui::TextUnformatted(title);
		if (raw.empty())
			ImGui::TextDisabled("尚无记录");
		else
		{
			ImGui::SameLine();
			if (ImGui::SmallButton("复制记录")) ImGui::SetClipboardText(raw.c_str());

			// 保留完整记录供复制，面板先排出开头 6 KiB，较长的输入也能马上查看。
			size_t shown = std::min(raw.size(), size_t(6144));
			while (shown < raw.size() && (static_cast<unsigned char>(raw[shown]) & 0xc0) == 0x80) --shown;
			if (shown < raw.size())
			{
				ImGui::SameLine();
				ImGui::TextDisabled("预览 %zu / %zu 字节", shown, raw.size());
			}
			if (ImGui::BeginChild("##raw", {0, ImGui::GetTextLineHeightWithSpacing() * 4.5f}, true))
			{
				ImGui::PushTextWrapPos(0.f);
				ImGui::TextUnformatted(raw.data(), raw.data() + shown);
				ImGui::PopTextWrapPos();
			}
			ImGui::EndChild();
		}
		ImGui::PopID();
	}

	void DrawNpcRequests(const NpcState* state, uint64_t& request)
	{
		const NpcDecisionTrace* selected = Selected(state, request);
		if (!selected)
		{
			ImGui::TextWrapped("角色尚未向模型提问。播放场景后，在游戏视图按 T 和他交谈。");
			return;
		}
		if (request && request != selected->ID) request = 0;
		const float follow = ImGui::CalcTextSize("跟随最新").x + ImGui::GetStyle().FramePadding.x * 2;
		ImGui::SetNextItemWidth(-follow - ImGui::GetStyle().ItemSpacing.x);
		if (ImGui::BeginCombo("##请求", Label(*selected).c_str()))
		{
			for (auto it = state->Decisions.rbegin(); it != state->Decisions.rend(); ++it)
			{
				if (ImGui::Selectable(Label(*it).c_str(), it->ID == selected->ID))
				{
					request = it->ID;
					selected = &*it;
				}
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		if (ImGui::Button("跟随最新"))
		{
			request = 0;
			selected = &state->Decisions.back();
		}

		if (!selected->Choice.empty()) ImGui::TextWrapped("选中  %s", selected->Choice.c_str());
		if (!selected->Action.empty()) ImGui::TextWrapped("执行动作  %s", selected->Action.c_str());
		if (!selected->Outcome.empty()) ImGui::TextWrapped("执行记录  %s", selected->Outcome.c_str());
		if (selected->Target) ImGui::Text("目标实体  %llu", static_cast<unsigned long long>(selected->Target));

		if (!selected->Candidates.empty() && ImGui::TreeNode("本次提供的选项"))
		{
			for (const DecisionChoice& option : selected->Candidates)
			{
				if (option.ID == selected->Choice)
					ImGui::TextWrapped("已选  %s  %s", option.ID.c_str(), option.Description.c_str());
				else ImGui::TextWrapped("%s  %s", option.ID.c_str(), option.Description.c_str());
			}
			ImGui::TreePop();
		}

		if (ImGui::BeginTabBar("请求正文"))
		{
			if (ImGui::BeginTabItem("模型"))
			{
				if (selected->ModelInput.find(";base64,[omitted ") != std::string::npos)
					ImGui::TextDisabled("记录保留图片类型和长度，省略图片编码。");
				DrawRaw("推理输入 JSON", selected->ModelInput);
				DrawRaw("模型生成的原文", selected->ModelOutput);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("决策服务"))
			{
				if (selected->Input.find("[图片数据已省略，") != std::string::npos)
					ImGui::TextDisabled("记录保留图片编号和长度，省略图片编码。");
				DrawRaw("引擎发出的 JSON", selected->Input);
				DrawRaw("服务返回的 JSON", selected->Output);
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
	}

	//========== 角色动作库 ==========

	void DrawNpcActions(const NpcDefinition& role, const NpcState* state, uint64_t request)
	{
		const NpcDecisionTrace* selected = Selected(state, request);
		if (selected)
		{
			ImGui::TextUnformatted(Label(*selected).c_str());
			if (!selected->Intent.empty()) ImGui::TextWrapped("选中的意图  %s", selected->Intent.c_str());
			if (!selected->Action.empty()) ImGui::TextWrapped("对应动作  %s", selected->Action.c_str());
			else ImGui::TextDisabled("这次请求没有执行动作");
		}
		if (state && !state->Action.empty())
			ImGui::TextWrapped("角色现在的动作  %s  /  %s", state->Action.c_str(), state->Stage.c_str());
		ImGui::Separator();

		if (ImGui::BeginTable("动作节点", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
		{
			ImGui::TableSetupColumn("行为树节点");
			ImGui::TableSetupColumn("执行方法");
			ImGui::TableSetupColumn("本次选择");
			ImGui::TableHeadersRow();
			for (const BehaviorNode& node : role.Tree.GetNodes())
			{
				if (node.Kind != BehaviorKind::Action) continue;
				ImGui::TableNextRow();
				ImGui::TableNextColumn(); ImGui::TextWrapped("%s", node.Name.c_str());
				ImGui::TableNextColumn(); ImGui::TextWrapped("%s", node.Action.c_str());
				ImGui::TableNextColumn();
				if (selected && !selected->Action.empty() && node.Action == selected->Action)
					ImGui::TextUnformatted("已选");
			}
			ImGui::EndTable();
		}
	}
}
