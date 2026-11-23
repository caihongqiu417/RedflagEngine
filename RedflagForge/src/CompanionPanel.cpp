#include "CompanionPanel.h"
#include "UI/NpcDecisionView.h"
#include "Redflag/AI/NpcSystem.h"
#include "Redflag/Scene/Entity.h"
#include <imgui.h>

namespace Redflag
{
	void CompanionPanel::OpenAssets()
	{
		m_AuthorActor = 0;
		m_Authoring = true;
	}

	void CompanionPanel::DrawInspector(Entity actor, bool playing)
	{
		NpcComponent& npc = actor.GetComponent<NpcComponent>();
		if (ImGui::Button("编辑角色资产"))
		{
			m_Asset = npc.Definition;
			m_AuthorActor = actor.GetUUID();
			m_Authoring = true;
		}
		if (!playing && !m_Asset.empty() && m_Asset != npc.Definition)
		{
			if (ImGui::Button("绑定当前角色资产")) npc.Definition = m_Asset;
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", m_Asset.c_str());
		}
		if (playing && ImGui::Button("本局请求与动作"))
		{
			if (m_Actor != actor.GetUUID()) m_Request = 0;
			m_Actor = actor.GetUUID();
			m_Open = true;
		}
	}

	void CompanionPanel::Stop()
	{
		m_Voice.Reset();
		m_SpeechCursor = 0;
		m_Request = 0;
		m_Chat = {};
		m_Speaker = 0;
	}

	void CompanionPanel::Update(Scene& scene)
	{
		if (!scene.GetNpcs()) return;
		const auto hud = scene.GetRegistry().view<HudComponent>();
		const uint64_t speaker = hud.empty() ? 0 : hud.get<HudComponent>(*hud.begin()).ConversationActor;
		if (speaker != m_Speaker)
		{
			m_Voice.Reset();
			m_SpeechCursor = 0;
			m_Chat = {};
			m_Speaker = speaker;
		}
		Entity actor = scene.GetEntityByUUID(m_Speaker);
		m_Voice.UpdatePlayback();
		if (actor && actor.HasComponent<NpcComponent>())
		{
			// 游戏界面关闭输入框时，场景指定的角色仍可播报。按事件编号读取同一帧里的所有回执。
			m_Voice.UpdateInput(scene, m_Speaker, [&](const std::string& text) { scene.GetNpcs()->Say(m_Speaker, text); });
			const std::string& voice = actor.GetComponent<NpcComponent>().VoiceOverride;
			while (const NpcEvent* event = scene.GetNpcs()->NextEvent(m_Speaker, m_SpeechCursor))
			{
				m_Chat.Receive(*event);
				if (event->Kind == "玩家" || event->Kind == "中止") m_Voice.StopPlayback();
				if (!event->Speech.empty())
				{
					const NpcDefinition* profile = scene.GetNpcs()->GetDefinition(m_Speaker);
					if (profile) m_Voice.Speak(m_Speaker, event->Speech, voice.empty() ? profile->Voice : voice);
				}
			}
		}
	}

	void CompanionPanel::DrawGame(Scene& scene, const ConversationStyle& style, ImVec2 corner, ImVec2 size)
	{
		Entity actor = scene.GetEntityByUUID(m_Speaker);
		if (actor && actor.HasComponent<NpcComponent>() && scene.GetNpcs())
			m_Chat.Draw(actor, *scene.GetNpcs(), m_Voice, style, corner, size);
	}

	void CompanionPanel::Draw(Scene& scene, bool playing, const std::function<void(uint64_t)>& drawPhoto)
	{
		Entity actor = scene.GetEntityByUUID(m_Actor);
		if (m_Authoring)
		{
			// 从实体进入时可以预览它的动作剪辑。浏览其他资产后，不再把旧实体的状态叠在新角色上。
			Entity owner = scene.GetEntityByUUID(m_AuthorActor);
			const bool bound = owner && owner.HasComponent<NpcComponent>() && owner.GetComponent<NpcComponent>().Definition == m_Asset;
			const NpcState* live = bound && scene.GetNpcs() ? scene.GetNpcs()->Inspect(m_AuthorActor) : nullptr;
			std::vector<std::string> clips;
			const ModelAnimation* animation = nullptr;
			if (bound && owner.HasComponent<ModelRendererComponent>())
			{
				const auto& model = owner.GetComponent<ModelRendererComponent>().ModelAsset;
				if (model && model->GetAnimation())
				{
					animation = model->GetAnimation().get();
					for (const auto& clip : animation->GetClips()) clips.push_back(clip.Name);
				}
			}
			auto inspect = [&](const NpcDefinition& role) {
				if (!bound) return;
				for (const NpcIntent& intent : role.Intents)
					if (intent.Target == "teammate")
						m_Author.DrawTeammateCheck(intent, CheckTeammateIntent(scene, m_AuthorActor, role, intent));
			};
			m_Author.Draw(m_Asset, live ? &live->Trace : nullptr, live ? live->Stage : "", clips, animation, inspect, &m_Authoring);
		}
		if (!m_Open || !playing || !actor || !actor.HasComponent<NpcComponent>()) return;

		ImGui::SetNextWindowSize({640, 620}, ImGuiCond_FirstUseEver);
		const std::string title = actor.GetName() + " · 本局请求与动作###NpcRuntimeInspector";
		if (!ImGui::Begin(title.c_str(), &m_Open)) { ImGui::End(); return; }
		NpcComponent& npc = actor.GetComponent<NpcComponent>();
		NpcSystem* game = scene.GetNpcs();
		const NpcState* state = game ? game->Inspect(m_Actor) : nullptr;

		NpcDefinition* definition = game->GetDefinition(m_Actor);
		//========== 请求记录、动作库与创作工具 ==========

		if (ImGui::BeginTabBar("角色调试"))
		{
			if (ImGui::BeginTabItem("请求记录"))
			{
				DrawNpcRequests(state, m_Request);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("动作库"))
			{
				if (definition)
				{
					DrawNpcActions(*definition, state, m_Request);
					ImGui::Checkbox("显示行为树", &m_ShowTree);
				}
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("角色工具"))
			{
				if (definition)
				{
					if (ImGui::Button("编辑角色资产"))
					{
						m_Asset = npc.Definition;
						m_AuthorActor = m_Actor;
						m_Authoring = true;
					}
					ImGui::TextWrapped("音色  %s", (npc.VoiceOverride.empty() ? definition->Voice : npc.VoiceOverride).c_str());
				}
				if (drawPhoto) drawPhoto(m_Actor);
				if (m_Speaker == m_Actor) m_Voice.DrawTools();
				if (state && state->Use.IsRunning()) ImGui::ProgressBar(state->Use.Progress());
				if (state && ImGui::CollapsingHeader("对局记录"))
					for (auto it = state->Events.rbegin(); it != state->Events.rend(); ++it)
						ImGui::TextWrapped("%.2f  %s  %s", it->Time, it->Kind.c_str(), it->Text.c_str());
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
		ImGui::End();
		if (m_ShowTree && definition)
			m_TreePanel.Draw(definition->Tree, definition->TreeFile, state ? &state->Trace : nullptr, "角色实例 · 行为树",
				state ? &state->Blackboard : nullptr);
	}
}
