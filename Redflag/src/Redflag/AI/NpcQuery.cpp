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

	std::vector<NpcCandidate> FindCandidates(Scene& scene, uint64_t actor, const NpcDefinition& definition, const NpcMemory& memory, bool hasCamera)
	{
		Entity self = scene.GetEntityByUUID(actor);
		if (!self || !self.HasComponent<NpcComponent>()) return {};
		const uint64_t leader = self.GetComponent<NpcComponent>().Leader;
		const glm::vec3 origin = self.GetComponent<TransformComponent>().Translation;
		std::vector<NpcCandidate> choices;

		for (const NpcIntent& intent : definition.Intents)
		{
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
				else if (target.HasComponent<InteractionComponent>() && target.HasComponent<HealthComponent>())
				{
					const InteractionComponent& use = target.GetComponent<InteractionComponent>();
					const HealthComponent& health = target.GetComponent<HealthComponent>();
					offered = (intent.Target == "interaction" || (intent.Target == "teammate" && teammate)) &&
						use.Action == intent.Key && health.Current < health.Maximum && (!use.OnlyWhenDown || health.Current <= 0);
				}
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
