#include "rfpch.h"
#include "WorldActions.h"
#include "ColliderBounds.h"
#include "HandContact.h"
#include "Redflag/Scene/Entity.h"
#include <limits>

namespace Redflag
{
	//========== 角色状态与伤害 ==========

	static bool CanAct(Entity actor)
	{
		return actor && (!actor.HasComponent<HealthComponent>() || actor.GetComponent<HealthComponent>().Current > 0.0f);
	}

	bool ApplyDamage(Scene& scene, uint64_t source, uint64_t target, float amount)
	{
		Entity victim = scene.GetEntityByUUID(target);
		if (!victim || !victim.HasComponent<HealthComponent>() || amount <= 0.0f)
			return false;

		HealthComponent& health = victim.GetComponent<HealthComponent>();
		Entity attacker = scene.GetEntityByUUID(source);
		if (attacker && attacker.HasComponent<HealthComponent>() && !health.Team.empty() &&
			attacker.GetComponent<HealthComponent>().Team == health.Team)
			return false;

		const float previous = health.Current;
		health.Current = std::max(0.0f, previous - amount);
		return previous != health.Current;
	}

	//========== 距离与遮挡 ==========

	bool CanReach(Scene& scene, uint64_t actor, uint64_t target, float range)
	{
		Entity from = scene.GetEntityByUUID(actor), to = scene.GetEntityByUUID(target);
		if (!CanAct(from) || !to || from == to || range < 0.0f)
			return false;

		const glm::vec3 start = from.GetComponent<TransformComponent>().Translation;
		const glm::vec3 finish = to.GetComponent<TransformComponent>().Translation;
		if (glm::distance(start, finish) > range) return false;

		// 双脚的原点就在地表。遮挡从身体查到物体，避免脚底的砂石挡住伸手救人。
		const auto body = GetColliderBounds(from), object = GetColliderBounds(to);
		const glm::vec3 origin = body ? body->Center : start;
		const glm::vec3 delta = (object ? object->Center : finish) - origin;
		const auto hit = scene.Raycast(origin, delta, glm::length(delta), actor);
		return !hit || hit->Entity == target;
	}

	// 配置了手部动作的角色只允许向本次碰到的目标提交结果。没有播放交互的宿主仍沿用物理距离检查。
	static bool Touches(Scene& scene, Entity actor, uint64_t target, const std::string& action)
	{
		if (!actor.HasComponent<HandContact>()) return true;
		const auto& hand = actor.GetComponent<HandContact>();
		return hand.Target == target && hand.Action == action && !hand.Passed && AtContact(scene,actor.GetUUID());
	}

	//========== 拾取与交付 ==========

	ActionResult CheckPickup(Scene& scene, uint64_t actor, uint64_t target, float range)
	{
		Entity owner = scene.GetEntityByUUID(actor), object = scene.GetEntityByUUID(target);
		if (!object) return ActionResult::MissingTarget;
		if (!CanAct(owner) || !owner.HasComponent<InventoryComponent>() || !object.HasComponent<PickupComponent>())
			return ActionResult::Unavailable;
		if (!CanReach(scene, actor, target, range)) return ActionResult::OutOfReach;

		const PickupComponent& pickup = object.GetComponent<PickupComponent>();
		if (pickup.Item.empty() || pickup.Count <= 0) return ActionResult::Unavailable;
		const auto& stock = owner.GetComponent<InventoryComponent>().Items;
		const auto found = stock.find(pickup.Item);
		if (found != stock.end() && found->second > std::numeric_limits<int>::max() - pickup.Count)
			return ActionResult::Unavailable;
		return ActionResult::Completed;
	}

	ActionResult CollectItem(Scene& scene, uint64_t actor, uint64_t target, float range)
	{
		const ActionResult result = CheckPickup(scene, actor, target, range);
		if (result != ActionResult::Completed) return result;

		Entity owner = scene.GetEntityByUUID(actor), object = scene.GetEntityByUUID(target);
		if (!Touches(scene,owner,target,"pickup")) return ActionResult::OutOfReach;
		CommitContact(scene,actor);
		const PickupComponent& pickup = object.GetComponent<PickupComponent>();
		scene.GetEntityByUUID(actor).GetComponent<InventoryComponent>().Items[pickup.Item] += pickup.Count;
		scene.DestroyEntity(object);
		return ActionResult::Completed;
	}

	ActionResult GiveItem(Scene& scene, uint64_t actor, uint64_t recipient, const std::string& item, int count, float range)
	{
		Entity giver = scene.GetEntityByUUID(actor), receiver = scene.GetEntityByUUID(recipient);
		if (!receiver) return ActionResult::MissingTarget;
		if (!CanAct(giver) || !giver.HasComponent<InventoryComponent>() || !receiver.HasComponent<InventoryComponent>() || count <= 0)
			return ActionResult::Unavailable;
		if (!CanReach(scene, actor, recipient, range)) return ActionResult::OutOfReach;

		auto& stock = giver.GetComponent<InventoryComponent>().Items;
		const auto found = stock.find(item);
		if (found == stock.end() || found->second < count) return ActionResult::Unavailable;
		if (!Touches(scene,giver,recipient,"give")) return ActionResult::OutOfReach;
		int& other = receiver.GetComponent<InventoryComponent>().Items[item];
		if (other > std::numeric_limits<int>::max() - count) return ActionResult::Unavailable;
		CommitContact(scene,actor);
		other += count;
		found->second -= count;
		if (found->second == 0) stock.erase(found);
		return ActionResult::Completed;
	}

	//========== 持续交互 ==========

	ActionResult Interaction::Start(Scene& scene, uint64_t actor, uint64_t target, const std::string& action)
	{
		m_Actor = actor;
		m_Target = target;
		m_Action = action;
		m_Progress = m_Elapsed = 0.0f;
		m_Result = ActionResult::Running;
		return Update(scene, 0.0f);
	}

	ActionResult Interaction::Update(Scene& scene, float seconds)
	{
		if (m_Result != ActionResult::Running) return m_Result;
		Entity target = scene.GetEntityByUUID(m_Target);
		if (!target) return m_Result = ActionResult::MissingTarget;
		if (!CanAct(scene.GetEntityByUUID(m_Actor)) || !target.HasComponent<HealthComponent>() || !target.HasComponent<InteractionComponent>())
			return m_Result = ActionResult::Unavailable;

		const InteractionComponent& interaction = target.GetComponent<InteractionComponent>();
		HealthComponent& health = target.GetComponent<HealthComponent>();
		// 两个队友可以同时开始救援。已有一人扶起目标时，另一人就结束这次交互。
		if (m_Action != interaction.Action || health.Current >= health.Maximum || interaction.Restore <= 0.0f ||
			(interaction.OnlyWhenDown && health.Current > 0.0f))
			return m_Result = ActionResult::Unavailable;
		if (!CanReach(scene, m_Actor, m_Target, interaction.Range))
			return m_Result = ActionResult::OutOfReach;

		m_Elapsed += std::max(0.0f, seconds);
		m_Progress = interaction.Seconds > 0.0f ? std::min(1.0f, m_Elapsed / interaction.Seconds) : 1.0f;
		if (m_Progress < 1.0f) return ActionResult::Running;

		Entity actor = scene.GetEntityByUUID(m_Actor);
		if (!Touches(scene,actor,m_Target,m_Action)) return m_Result = ActionResult::OutOfReach;
		CommitContact(scene,m_Actor);
		health.Current = std::min(health.Maximum, health.Current + interaction.Restore);
		return m_Result = ActionResult::Completed;
	}

	void Interaction::Cancel()
	{
		if (m_Result == ActionResult::Running) m_Result = ActionResult::Cancelled;
	}
}
