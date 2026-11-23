#include "rfpch.h"
#include "GameplayBindings.h"
#include "AnimatorBindings.h"
#include "Redflag/Animation/ActorAnimator.h"
#include "Redflag/Gameplay/GameHud.h"
#include "Redflag/Gameplay/TerrainTraversal.h"
#include "Redflag/Gameplay/TraversalRig.h"
#include "Redflag/Gameplay/WorldActions.h"
#include "Redflag/Scene/Entity.h"
#include <mono/metadata/object.h>
#include <mono/metadata/appdomain.h>

namespace Redflag
{
	namespace {
		Scene& World() { return *ScriptEngine::GetSceneContext(); }

		std::string Text(MonoString* value)
		{
			char* utf8 = mono_string_to_utf8(value);
			std::string text(utf8);
			mono_free(utf8);
			return text;
		}

		// ---- 地面移动与动作片段 ----
		bool Move(uint64_t actor, const glm::vec3* delta, float radius)
		{
			Entity entity = World().GetEntityByUUID(actor);
			if (!entity) return false;
			const glm::vec3 start = entity.GetComponent<TransformComponent>().Translation;
			MoveCharacter(World(), actor, *delta, radius, 0.f);
			return entity.GetComponent<TransformComponent>().Translation != start;
		}

		int Traverse(uint64_t actor, const glm::vec3* delta, float radius, float seconds, int input)
		{
			return int(MoveCharacter(World(), actor, *delta, radius, seconds, TraversalInput(input)));
		}

		void StopTraversal(uint64_t actor) { CancelTraversal(World(), actor); }

		void SetMovementRoot(uint64_t actor, MonoString* node)
		{
			Entity entity = World().GetEntityByUUID(actor);
			if (entity) World().GetRegistry().get_or_emplace<TraversalRig>(static_cast<entt::entity>(entity)).MovementRoot = Text(node);
		}

		void SetTraversalLimb(uint64_t actor, int index, MonoString* upper, MonoString* lower, MonoString* tip)
		{
			Entity entity = World().GetEntityByUUID(actor);
			if (!entity || index < 0 || index >= 4) return;
			auto& rig = World().GetRegistry().get_or_emplace<TraversalRig>(static_cast<entt::entity>(entity));
			rig.Limbs[index] = { Text(upper), Text(lower), Text(tip) };
		}

		void SelectClip(uint64_t actor, MonoString* name, bool locomotion)
		{
			Entity entity = World().GetEntityByUUID(actor);
			if (!entity || !entity.HasComponent<ModelAnimationComponent>()) return;
			ModelAnimationComponent& animation = entity.GetComponent<ModelAnimationComponent>();
			const std::string clip = Text(name);
			auto* animator = World().GetRegistry().try_get<ActorAnimator>(static_cast<entt::entity>(entity));
			if (animator && animator->Clip != clip) animator->Timeline.Cancel();
			if (animation.Clip != clip) {
				if (animator && animation.Clip == animator->Clip) animation.Loop = true;
				animation.Clip = clip;
				animation.Time = 0.f;
			}
			animation.MatchTravel = locomotion;
			if (locomotion) animation.Loop = true;
			animation.Playing = true;
		}

		void PlayClip(uint64_t actor, MonoString* name) { SelectClip(actor, name, false); }
		void PlayLocomotion(uint64_t actor, MonoString* name) { SelectClip(actor, name, true); }

		void HoldPose(uint64_t actor, MonoString* name, float seconds)
		{
			Entity entity = World().GetEntityByUUID(actor);
			if (!entity || !entity.HasComponent<ModelAnimationComponent>()) return;
			ModelAnimationComponent& animation = entity.GetComponent<ModelAnimationComponent>();
			auto* animator = World().GetRegistry().try_get<ActorAnimator>(static_cast<entt::entity>(entity));
			if (animator) animator->Timeline.Cancel();
			animation.Clip = Text(name);
			animation.Time = seconds;
			animation.Playing = false;
			animation.MatchTravel = false;
		}

		// ---- 生命、补给与交互 ----
		float Health(uint64_t actor)
		{
			Entity entity = World().GetEntityByUUID(actor);
			return entity && entity.HasComponent<HealthComponent>() ? entity.GetComponent<HealthComponent>().Current : 0.f;
		}

		float MaxHealth(uint64_t actor)
		{
			Entity entity = World().GetEntityByUUID(actor);
			return entity && entity.HasComponent<HealthComponent>() ? entity.GetComponent<HealthComponent>().Maximum : 0.f;
		}

		int ItemCount(uint64_t actor, MonoString* item)
		{
			Entity entity = World().GetEntityByUUID(actor);
			if (!entity || !entity.HasComponent<InventoryComponent>()) return 0;
			const auto& items = entity.GetComponent<InventoryComponent>().Items;
			const auto found = items.find(Text(item));
			return found != items.end() ? found->second : 0;
		}

		bool Heal(uint64_t actor, MonoString* item, float amount)
		{
			Entity entity = World().GetEntityByUUID(actor);
			if (!entity || !entity.HasComponent<InventoryComponent>() || !entity.HasComponent<HealthComponent>()) return false;
			auto& items = entity.GetComponent<InventoryComponent>().Items;
			const auto stock = items.find(Text(item));
			HealthComponent& health = entity.GetComponent<HealthComponent>();
			if (stock == items.end() || stock->second <= 0 || health.Current <= 0 || health.Current >= health.Maximum || amount <= 0) return false;
			if (--stock->second == 0) items.erase(stock);
			health.Current = std::min(health.Maximum, health.Current+amount);
			return true;
		}

		bool Hit(uint64_t source, uint64_t target, float range, float amount)
		{
			return CanReach(World(),source,target,range) && ApplyDamage(World(),source,target,amount);
		}

		bool CanCollect(uint64_t actor, uint64_t target, float range)
		{
			return CheckPickup(World(),actor,target,range) == ActionResult::Completed;
		}

		bool Collect(uint64_t actor, uint64_t target, float range)
		{
			return CollectItem(World(),actor,target,range) == ActionResult::Completed;
		}

		int Interact(uint64_t actor, uint64_t target, MonoString* action, float seconds, bool start)
		{
			Entity entity = World().GetEntityByUUID(actor);
			if (!entity) return int(ActionResult::Unavailable);
			Interaction& use = World().GetRegistry().get_or_emplace<Interaction>(static_cast<entt::entity>(entity));
			if (start) return int(use.Start(World(),actor,target,Text(action)));
			return int(use.Update(World(),seconds));
		}

		// ---- 游戏镜头与屏幕提示 ----
		float SweepSphere(uint64_t actor, const glm::vec3* origin, const glm::vec3* direction, float radius, float distance)
		{
			return World().SweepSphere(*origin, *direction, radius, distance, actor);
		}

		uint64_t Ray(uint64_t actor, const glm::vec3* origin, const glm::vec3* direction, float distance, glm::vec3* point)
		{
			const auto hit = World().Raycast(*origin,*direction,distance,actor);
			if (hit) { *point = hit->Position; return hit->Entity; }
			float length = glm::length(*direction);
			*point = length > 0 ? *origin+*direction*(distance/length) : *origin;
			return 0;
		}

		bool Damage(uint64_t actor, uint64_t target, float amount) { return ApplyDamage(World(),actor,target,amount); }

		MonoString* Name(uint64_t id)
		{
			Entity entity = World().GetEntityByUUID(id);
			return mono_string_new(mono_domain_get(),entity ? entity.GetName().c_str() : "");
		}

		void Pulse(uint64_t actor, const glm::vec3* from, const glm::vec3* to)
		{
			Entity entity=World().GetEntityByUUID(actor);
			if (!entity) return;
			auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(120);
			World().GetRegistry().emplace_or_replace<PulseTrace>(static_cast<entt::entity>(entity),*from,*to,until);
		}

		bool Socket(uint64_t actor, MonoString* name, const glm::vec3* offset, glm::vec3* point)
		{
			Entity entity = World().GetEntityByUUID(actor);
			glm::mat4 bone;
			if (!entity || !entity.HasComponent<ModelAnimationComponent>() ||
				!entity.GetComponent<ModelAnimationComponent>().GetNodeTransform(Text(name),bone)) return false;
			*point = entity.GetComponent<TransformComponent>().GetTransform() * bone * glm::vec4(*offset,1);
			return true;
		}

		void BodyHeight(uint64_t actor, float height)
		{
			Entity entity = World().GetEntityByUUID(actor);
			if (!entity || !entity.HasComponent<BoxCollider3DComponent>() || height<=0) return;
			auto& box = entity.GetComponent<BoxCollider3DComponent>();
			const float scale = std::abs(entity.GetComponent<TransformComponent>().Scale.y);
			if (scale > 0) box.Center.y = box.HalfSize.y = .5f*height/scale;
		}

		void Label(uint64_t actor, const glm::vec3* color, float height)
		{
			Entity entity = World().GetEntityByUUID(actor);
			if (entity) World().GetRegistry().emplace_or_replace<Nameplate>(static_cast<entt::entity>(entity), *color, height);
		}

		void Hud(uint64_t actor, MonoString* title, MonoString* objective, MonoString* status, MonoString* controls, bool finished)
		{
			Entity entity = World().GetEntityByUUID(actor);
			if (!entity) return;
			GameHud& hud = World().GetRegistry().get_or_emplace<GameHud>(entity);
			hud.Text["title"] = Text(title);
			hud.Text["objective"] = Text(objective);
			hud.Text["status"] = Text(status);
			hud.Text["controls"] = Text(controls);
			hud.Finished = finished;
		}

		void HudText(uint64_t actor, MonoString* field, MonoString* value)
		{
			Entity entity = World().GetEntityByUUID(actor);
			if (entity) World().GetRegistry().get_or_emplace<GameHud>(entity).Text[Text(field)] = Text(value);
		}

		void HudMeter(uint64_t actor, MonoString* field, float fraction)
		{
			Entity entity = World().GetEntityByUUID(actor);
			if (entity) World().GetRegistry().get_or_emplace<GameHud>(entity).Meters[Text(field)] = std::clamp(fraction, 0.f, 1.f);
		}
	}

	void RegisterGameplayCalls()
	{
		mono_add_internal_call("Redflag.Hud::WriteText", (const void*)HudText);
		mono_add_internal_call("Redflag.Hud::WriteMeter", (const void*)HudMeter);
		RegisterAnimatorBindings();
		mono_add_internal_call("Redflag.GameWorld::Move", (const void*)Move);
		mono_add_internal_call("Redflag.GameWorld::Traverse", (const void*)Traverse);
		mono_add_internal_call("Redflag.GameWorld::CancelTraversal", (const void*)StopTraversal);
		mono_add_internal_call("Redflag.GameWorld::SetTraversalLimb", (const void*)SetTraversalLimb);
		mono_add_internal_call("Redflag.GameWorld::SetMovementRoot", (const void*)SetMovementRoot);
		mono_add_internal_call("Redflag.GameWorld::PlayClip", (const void*)PlayClip);
		mono_add_internal_call("Redflag.GameWorld::PlayLocomotion", (const void*)PlayLocomotion);
		mono_add_internal_call("Redflag.GameWorld::Pose", (const void*)HoldPose);
		mono_add_internal_call("Redflag.GameWorld::MaxHealth", (const void*)MaxHealth);
		mono_add_internal_call("Redflag.GameWorld::Health", (const void*)Health);
		mono_add_internal_call("Redflag.GameWorld::ItemCount", (const void*)ItemCount);
		mono_add_internal_call("Redflag.GameWorld::Heal", (const void*)Heal);
		mono_add_internal_call("Redflag.GameWorld::Hit", (const void*)Hit);
		mono_add_internal_call("Redflag.GameWorld::CanCollect", (const void*)CanCollect);
		mono_add_internal_call("Redflag.GameWorld::Collect", (const void*)Collect);
		mono_add_internal_call("Redflag.GameWorld::Interact", (const void*)Interact);
		mono_add_internal_call("Redflag.GameWorld::Hud", (const void*)Hud);
		mono_add_internal_call("Redflag.GameWorld::Label", (const void*)Label);
		mono_add_internal_call("Redflag.GameWorld::SweepSphere", (const void*)SweepSphere);
		mono_add_internal_call("Redflag.GameWorld::Ray", (const void*)Ray);
		mono_add_internal_call("Redflag.GameWorld::Damage", (const void*)Damage);
		mono_add_internal_call("Redflag.GameWorld::Name", (const void*)Name);
		mono_add_internal_call("Redflag.GameWorld::Pulse", (const void*)Pulse);
		mono_add_internal_call("Redflag.GameWorld::Socket", (const void*)Socket);
		mono_add_internal_call("Redflag.GameWorld::BodyHeight", (const void*)BodyHeight);
	}
}
