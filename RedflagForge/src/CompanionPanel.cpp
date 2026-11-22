#include "CompanionPanel.h"
#include "Redflag/AI/NpcSystem.h"
#include "Redflag/Scene/Entity.h"
#include <imgui.h>

namespace Redflag
{
	void CompanionPanel::DrawMenu()
	{
		if (ImGui::MenuItem("AI 队友", "T")) m_Open = true;
	}

	void CompanionPanel::Stop()
	{
		m_Voice.Reset();
		m_LastSpeech = -1.0;
	}

	void CompanionPanel::Draw(Scene& scene, bool playing, const std::function<void(uint64_t)>& drawPhoto)
	{
		m_Voice.UpdatePlayback();
		if (playing && scene.GetNpcs() && m_Actor)
		{
			// 通讯窗口收起时也能听见新回执。只播动作真正完成后留下的台词。
			const NpcState* speaker = scene.GetNpcs()->Inspect(m_Actor);
			NpcDefinition* profile = scene.GetNpcs()->GetDefinition(m_Actor);
			if (speaker && profile)
			{
				const std::string& voice = scene.GetEntityByUUID(m_Actor).GetComponent<NpcComponent>().VoiceOverride;
				for (const NpcEvent& event : speaker->Events)
				{
					if (event.Time <= m_LastSpeech) continue;
					if (event.Kind == "玩家" || event.Kind == "中止") m_Voice.StopPlayback();
					if (!event.Speech.empty()) m_Voice.Speak(event.Speech, voice.empty() ? profile->Voice : voice);
					m_LastSpeech = event.Time;
				}
			}
		}
		if (playing && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_T, false))
		{
			m_Open = !m_Open;
			if (!m_Open) m_Voice.Reset();
		}
		if (!m_Open) return;

		ImGui::SetNextWindowSize({570, 470}, ImGuiCond_FirstUseEver);
		if (!ImGui::Begin("队友通讯", &m_Open)) { ImGui::End(); return; }

		//========== 选择角色 ==========

		Entity actor = scene.GetEntityByUUID(m_Actor);
		const auto actors = scene.GetRegistry().view<NpcComponent>();
		if ((!actor || !actor.HasComponent<NpcComponent>()) && !actors.empty())
			actor = Entity(*actors.begin(), &scene);
		if (!actor || !actor.HasComponent<NpcComponent>())
		{
			ImGui::TextWrapped("当前场景没有 AI 角色。在层级中选中物体，在属性中添加「AI 角色」并选择角色定义。");
			ImGui::End();
			return;
		}

		if (ImGui::BeginCombo("队友", actor.GetName().c_str()))
		{
			for (entt::entity handle : actors)
			{
				Entity option(handle, &scene);
				ImGui::PushID(static_cast<int>(handle));
				if (ImGui::Selectable(option.GetName().c_str(), option == actor)) actor = option;
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		if (m_Actor != actor.GetUUID())
		{
			Stop();
			m_Actor = actor.GetUUID();
		}
		NpcComponent& npc = actor.GetComponent<NpcComponent>();
		NpcSystem* game = scene.GetNpcs();
		const NpcState* state = game ? game->Inspect(m_Actor) : nullptr;

		//========== 玩家发言与游戏回执 ==========

		if (playing && game)
		{
			if (drawPhoto) drawPhoto(m_Actor);
			m_Voice.Draw(scene, m_Actor, [&](const std::string& text) { game->Submit(m_Actor, text); });
			ImGui::SetNextItemWidth(-82);
			const bool entered = ImGui::InputTextWithHint("##speech", "告诉队友你现在需要什么", m_Text, sizeof(m_Text), ImGuiInputTextFlags_EnterReturnsTrue);
			ImGui::SameLine();
			if ((ImGui::Button("发送") || entered) && m_Text[0] && game->Submit(m_Actor, m_Text))
				m_Text[0] = 0;

			if (state)
			{
				if (state->Request) ImGui::TextUnformatted("队友正在考虑这次指令…");
				else if (!state->Action.empty()) ImGui::Text("正在执行  %s", state->Action.c_str());
				if (!state->Stage.empty()) ImGui::Text("当前动作  %s", state->Stage.c_str());
				if (state->Use.IsRunning()) ImGui::ProgressBar(state->Use.Progress());
				if (ImGui::CollapsingHeader("现场候选"))
					for (const NpcCandidate& c : state->Candidates)
						ImGui::TextWrapped("%s   %s   %.1f 米", c.ID.c_str(), c.Description.c_str(), c.Distance);

				ImGui::Separator();
				ImGui::BeginChild("对局记录", {0, ImGui::GetTextLineHeightWithSpacing()*7}, true);
				for (auto it = state->Events.rbegin(); it != state->Events.rend(); ++it)
					ImGui::TextWrapped("%.2f  %s   %s", it->Time, it->Kind.c_str(), it->Text.c_str());
				ImGui::EndChild();
			}
		}
		else ImGui::TextWrapped("按 F6 播放后可以和队友交谈。");

		//========== 角色定义与行为树 ==========

		NpcDefinition* definition = nullptr;
		if (game) definition = game->GetDefinition(m_Actor);
		else
		{
			if (m_Path != npc.Definition)
			{
				m_Path = npc.Definition;
				m_Error.clear();
				m_Editing.Load(std::filesystem::u8path(m_Path), m_Error);
			}
			if (m_Error.empty() && !m_Path.empty()) definition = &m_Editing;
		}
		if (definition)
		{
			ImGui::TextWrapped("音色  %s", (npc.VoiceOverride.empty() ? definition->Voice : npc.VoiceOverride).c_str());
			ImGui::Checkbox("显示行为树", &m_ShowTree);
			ImGui::SameLine();
			ImGui::Checkbox("角色创作", &m_Authoring);
		}
		else if (!m_Error.empty()) ImGui::TextWrapped("%s", m_Error.c_str());
		ImGui::End();
		if (m_ShowTree && definition) m_TreePanel.Draw(definition->Tree, definition->TreeFile, state ? &state->Trace : nullptr);
		if (m_Authoring)
		{
			std::vector<std::string> clips;
			const ModelAnimation* animation = nullptr;
			if (actor.HasComponent<ModelRendererComponent>())
			{
				const auto& model = actor.GetComponent<ModelRendererComponent>().ModelAsset;
				if (model && model->GetAnimation())
				{
					animation = model->GetAnimation().get();
					for (const auto& clip : animation->GetClips()) clips.push_back(clip.Name);
				}
			}
			if (m_Author.Draw(npc.Definition, state ? &state->Trace : nullptr, state ? state->Stage : "", clips, animation))
				m_Path.clear();
		}
	}
}
