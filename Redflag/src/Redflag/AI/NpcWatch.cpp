#include "rfpch.h"
#include "NpcSystem.h"
#include "NpcPhoto.h"
#include "Redflag/Gameplay/ColliderBounds.h"
#include "Redflag/Scene/Entity.h"

namespace Redflag
{
	// ---- 动作与视线 ----

	static const NpcCue* PlayingCue(Entity target, const NpcDefinition& role)
	{
		if (!target || !target.HasComponent<HealthComponent>() || !target.HasComponent<ModelAnimationComponent>()) return nullptr;
		const HealthComponent& health = target.GetComponent<HealthComponent>();
		const ModelAnimationComponent& animation = target.GetComponent<ModelAnimationComponent>();
		if (health.Current <= 0.f || !animation.Playing) return nullptr;
		for (const NpcCue& cue : role.Watch)
			if (health.Team == cue.Team && animation.Clip == cue.Clip) return &cue;
		return nullptr;
	}

	static bool InSight(Scene& scene, Entity owner, Entity target, const NpcDefinition& role)
	{
		const TransformComponent& pose = owner.GetComponent<TransformComponent>();
		const glm::vec3 eye = NpcEyePosition(pose, role);
		const auto bounds = GetColliderBounds(target);
		if (!bounds) return false;
		const glm::vec3 line = bounds->Center - eye;
		const float distance = glm::length(line);
		if (distance <= 0.f || distance > role.Sight) return false;

		// 对照角色相机的 16:9 画面，先排除镜头外的物体，再看最先碰到的是墙还是目标。
		const glm::vec3 local = glm::inverse(glm::quat(pose.Rotation)) * line;
		const float halfHeight = local.z * std::tan(glm::radians(role.VerticalFov) * .5f);
		if (local.z <= 0.f || std::abs(local.y) > halfHeight || std::abs(local.x) > halfHeight * (16.f / 9.f)) return false;
		const auto hit = scene.Raycast(eye, line / distance, distance, owner.GetUUID());
		return hit && hit->Entity == target.GetUUID();
	}

	// ---- 看到动作与电台提醒 ----

	void NpcSystem::Observe(uint64_t actor, NpcState& state, const NpcDefinition& role, float seconds)
	{
		NpcWatch& watch = state.Watch;
		Entity owner = m_Scene.GetEntityByUUID(actor);
		watch.NextScan -= seconds;
		const bool scan = watch.NextScan <= 0.f;
		if (scan)
		{
			watch.NextScan = .2f; // 每秒检查五次；缩短间隔可以认出更短的动作，但会增加射线查询。
			for (auto seen = watch.SeenClips.begin(); seen != watch.SeenClips.end();)
			{
				Entity target = m_Scene.GetEntityByUUID(seen->first);
				const NpcCue* cue = PlayingCue(target, role);
				if (cue && seen->second == cue->Clip && role.ObserveWorld && InSight(m_Scene, owner, target, role))
				{
					++seen;
					continue;
				}
				state.Memory.LoseSight(seen->first);
				if (watch.Request && seen->first == watch.Target)
				{
					CancelDecision(state, watch.Request);
					watch.Request = 0;
					Record(state, "观察结束", "没有再次看见刚才的动作，未发出提醒。", watch.Target);
				}
				// 转身时仍记得自己已见过这一次表演。目标换动作或离场后，下次才能再次提醒。
				if (cue && seen->second == cue->Clip) ++seen;
				else seen = watch.SeenClips.erase(seen);
			}
		}
		if (watch.Request)
		{
			const auto reply = TakeDecision(state, watch.Request);
			if (!reply) return;
			watch.Request = 0;
			Entity target = m_Scene.GetEntityByUUID(watch.Target);
			const NpcCue* cue = PlayingCue(target, role);
			const bool stillVisible = role.ObserveWorld && cue && cue->Clip == watch.SeenClips.at(watch.Target) &&
				InSight(m_Scene, owner, target, role);
			if (!stillVisible)
				Record(state, "观察结束", "没有再次看见刚才的动作，未发出提醒。", watch.Target);
			else if (reply->Status != DecisionStatus::Completed)
				Record(state, "观察未完成", reply->Error, watch.Target);
			else if (reply->Choice == "announce")
				Record(state, "主动报点", cue->Message, watch.Target, cue->Message);
			else
				Record(state, "静默观察", cue->Message, watch.Target);
		}

		if (!scan || !role.ObserveWorld || role.Watch.empty()) return;

		for (entt::entity handle : m_Scene.GetRegistry().view<HealthComponent, ModelAnimationComponent>())
		{
			Entity target(handle, &m_Scene);
			const uint64_t id = target.GetUUID();
			if (id == actor || watch.SeenClips.count(id)) continue;
			const NpcCue* cue = PlayingCue(target, role);
			if (!cue || !InSight(m_Scene, owner, target, role)) continue;

			state.Memory.Configure(role.Memory);
			state.Memory.See(id, cue->Message, SessionSeconds());
			DecisionRequest request;
			request.Prompt = "我刚亲眼看见：" + cue->Message + " 现在该开口提醒队友，还是安静记下？";
			request.Instructions = "按玩家约定的提醒方式选择。没有约定时，主动告诉队友。";
			request.Model = role.Model;
			if (!role.LeaderRelationship.empty()) request.Observations.push_back({"队友约定", role.LeaderRelationship});
			state.Memory.AddTo(request);
			request.Candidates = {{"announce", "现在通过电台提醒队友"}, {"quiet", "安静观察，只在记录中保留"}};
			watch.SeenClips[id] = cue->Clip;
			watch.Target = id;
			watch.Request = StartDecision(state, "主动观察", std::move(request), false, id);
			Record(state, "主动观察", cue->Message, id);
			break;
		}
	}
}
