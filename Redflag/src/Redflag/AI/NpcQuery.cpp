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

	std::vector<NpcCandidate> FindCandidates(Scene& scene, uint64_t actor, const NpcDefinition& definition)
	{
		Entity self = scene.GetEntityByUUID(actor);
		if (!self || !self.HasComponent<NpcComponent>()) return {};
		const uint64_t leader = self.GetComponent<NpcComponent>().Leader;
		const glm::vec3 origin = self.GetComponent<TransformComponent>().Translation;
		std::vector<NpcCandidate> choices;

		for (const NpcIntent& intent : definition.Intents)
		{
			// 同一个动作可能有几个目标。先检查库存或交互条件，再检查能否看见物体。
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
