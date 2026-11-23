#include "rfpch.h"
#include "NpcQuery.h"
#include "Redflag/Gameplay/WorldActions.h"
#include "Redflag/Scene/Entity.h"

namespace Redflag
{
	static int ItemCount(Entity owner, const std::string& item)
	{
		if (!owner.HasComponent<InventoryComponent>()) return 0;
		const auto& items = owner.GetComponent<InventoryComponent>().Items;
		const auto found = items.find(item);
		return found != items.end() ? found->second : 0;
	}

	// ---- 交互条件 ----

	static const char* InteractionUnavailable(Entity target, const NpcIntent& intent)
	{
		if (!target.HasComponent<InteractionComponent>()) return "目标没有交互组件。";
		if (!target.HasComponent<HealthComponent>()) return "目标没有生命值组件。";

		const auto& use = target.GetComponent<InteractionComponent>();
		const auto& health = target.GetComponent<HealthComponent>();
		if (use.Action != intent.Key) return "目标的交互动作与这项候选不同。";
		if (use.OnlyWhenDown && !(health.Current <= 0.f)) return "目标尚未倒地，这项交互只在倒地时可用。";
		if (!(health.Current < health.Maximum)) return "目标的生命值已满。";
		return nullptr;
	}

	NpcTeammateCheck CheckTeammateIntent(Scene& scene, uint64_t actor, const NpcDefinition& role, const NpcIntent& intent)
	{
		if (!role.ObserveWorld) return {{}, "角色未开启场景观察。"};
		Entity self = scene.GetEntityByUUID(actor);
		if (!self || !self.HasComponent<NpcComponent>()) return {{}, "当前场景找不到这个 AI 角色。"};
		uint64_t id = self.GetComponent<NpcComponent>().Leader;
		if (!id) return {{}, "角色还没有指定队友。"};
		if (id == actor) return {{}, "角色把自己设成了队友。"};

		Entity target = scene.GetEntityByUUID(id);
		if (!target) return {{}, "指定的队友已不在当前场景中。"};
		if (!target.HasComponent<TransformComponent>()) return {{}, "指定的队友没有场景位置。"};
		if (const char* reason = InteractionUnavailable(target, intent)) return {{}, reason};

		float distance = glm::distance(self.GetComponent<TransformComponent>().Translation,
			target.GetComponent<TransformComponent>().Translation);
		return {{intent.Key, intent.Key, intent.Description + " / " + target.GetName(), intent.Item, id, distance}};
	}

	// ---- 当前可选动作 ----

	std::vector<NpcCandidate> FindCandidates(Scene& scene, uint64_t actor, const NpcDefinition& definition, const NpcMemory& memory, bool hasCamera)
	{
		Entity self = scene.GetEntityByUUID(actor);
		if (!self || !self.HasComponent<NpcComponent>()) return {};
		const uint64_t leader = self.GetComponent<NpcComponent>().Leader;
		const glm::vec3 origin = self.GetComponent<TransformComponent>().Translation;
		std::vector<NpcCandidate> choices;

		for (const NpcIntent& intent : definition.Intents)
		{
			if (intent.Target == "injured_self")
			{
				if (!self.HasComponent<HealthComponent>() || ItemCount(self,intent.Item) <= 0) continue;
				const auto& health = self.GetComponent<HealthComponent>();
				if (health.Current > 0.f && health.Current < health.Maximum)
					choices.push_back({intent.Key, intent.Key, intent.Description, intent.Item, actor, 0.f});
				continue;
			}
			if (intent.Target == "self")
			{
				choices.push_back({intent.Key, intent.Key, intent.Description, {}, actor, 0.f});
				continue;
			}
			if (intent.Target == "teammate")
			{
				NpcTeammateCheck check = CheckTeammateIntent(scene, actor, definition, intent);
				if (!check.Reason) choices.push_back(std::move(check.Candidate));
				continue;
			}
			if (intent.Target == "surroundings")
			{
				if (hasCamera && definition.Memory.Pictures)
					choices.push_back({intent.Key, intent.Key, intent.Description, {}, 0, 0.f});
				continue;
			}

			// 这些动作针对一条消息或照片。物体还在不在，要等角色到了再看。
			if (intent.Target == "clue" || intent.Target == "search_area")
			{
				for (const NpcEvidence& note : memory.GetEvidence())
				{
					if (note.Source != EvidenceSource::Photo && note.Source != EvidenceSource::Reported) continue;
					if (intent.Target == "search_area" && (!note.Place || note.Visited)) continue;
					const std::string source = note.Source == EvidenceSource::Photo ? "照片中的判断：" : "队友转述：";
					// 两条消息可以都没有实体编号。模型按消息编号选择，换了排列顺序也能找回同一条。
					choices.push_back({intent.Key + std::to_string(note.Number), intent.Key, intent.Description + " / " + source + note.Text,
						{}, 0, note.Place ? glm::distance(origin, *note.Place) : 0.f, note.Number, note.Place});
				}
				continue;
			}

			// 拿取、维修等操作需要具体实体。在附近找符合条件的物品或设备，等实际接触时还要复查。
			const size_t start = choices.size();
			for (entt::entity handle : scene.GetRegistry().view<IDComponent, TransformComponent>())
			{
				Entity target(handle, &scene);
				const uint64_t id = target.GetUUID();
				if (id == actor) continue;
				const bool teammate = id == leader;
				const bool hasStock = ItemCount(self, intent.Item) > 0;
				bool offered = false;
				if (intent.Target == "leader") offered = teammate;
				else if (intent.Target == "inventory" || intent.Target == "promise")
					offered = teammate && hasStock && target.HasComponent<InventoryComponent>();
				else if (!definition.ObserveWorld) continue;
				else if (intent.Target == "pickup" || intent.Target == "sighting")
				{
					if (target.HasComponent<PickupComponent>())
					{
						const auto& pickup = target.GetComponent<PickupComponent>();
						offered = !pickup.Item.empty() && (intent.Item.empty() || pickup.Item == intent.Item) && pickup.Count > 0;
					}
				}
				else if (intent.Target == "interaction")
					offered = !InteractionUnavailable(target, intent);
				if (!offered) continue;
				const float distance = glm::distance(origin, target.GetComponent<TransformComponent>().Translation);
				if (!teammate && !CanReach(scene, actor, id, definition.Sight)) continue;
				choices.push_back({intent.Key, intent.Key, intent.Description + " / " + target.GetName(), intent.Item, id, distance});
			}
			// 给同一种动作的每个目标分配一个选项，模型的回答只能选中其中一个。
			std::sort(choices.begin() + start, choices.end(), [](const NpcCandidate& a, const NpcCandidate& b) {
				if (a.Distance == b.Distance) return a.Target < b.Target;
				return a.Distance < b.Distance;
			});
			for (size_t i = start + 1; i < choices.size(); ++i)
				choices[i].ID += std::to_string(i - start + 1);
		}
		return choices;
	}
}
