#include "rfpch.h"
#include "GameplayBindings.h"
#include "Redflag/AI/ActorBehavior.h"
#include "Redflag/Gameplay/ColliderBounds.h"
#include "Redflag/Gameplay/HandContact.h"
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

        //========== 脚本的黑板与行为树 ==========

        ActorBehavior* Behavior(uint64_t id)
        {
            Entity actor = World().GetEntityByUUID(id);
            if (!actor || !actor.HasComponent<BehaviorTreeComponent>()) return nullptr;
            auto& behavior = World().GetRegistry().get_or_emplace<ActorBehavior>(static_cast<entt::entity>(actor));
            return &behavior;
        }

        bool OpenBehavior(uint64_t id, ActorBehavior& behavior, MonoArray* conditions)
        {
            Entity actor = World().GetEntityByUUID(id);
            if (!behavior.Open(actor.GetComponent<BehaviorTreeComponent>().File)) {
                RF_CORE_ERROR("BehaviorTree: {0}", behavior.Error);
                return false;
            }

            auto& facts = behavior.Facts.Values;
            facts = behavior.SavedValues;
            const uintptr_t count = conditions ? mono_array_length(conditions) : 0;
            for (uintptr_t i = 0; i < count; ++i) {
                MonoString* key = mono_array_get(conditions, MonoString*, i);
                if (key) facts[Text(key)] = "true";
            }
            return true;
        }

        MonoString* ChooseBehavior(uint64_t id, MonoArray* conditions)
        {
            ActorBehavior* behavior = Behavior(id);
            if (!behavior || !OpenBehavior(id, *behavior, conditions)) return nullptr;
            if (behavior->Tree.UsesTasks() && behavior->Tree.Find(behavior->Tree.GetRoot())->Kind != BehaviorKind::Action)
                return nullptr;

            behavior->Trace = behavior->Tree.Tick(behavior->Facts, "");
            return mono_string_new(mono_domain_get(), behavior->Trace.Action.c_str());
        }

        void SetBehaviorValue(uint64_t id, MonoString* key, MonoString* value)
        {
            ActorBehavior* behavior = Behavior(id);
            if (!behavior || !key) return;
            if (value) behavior->SavedValues[Text(key)] = Text(value);
            else behavior->SavedValues.erase(Text(key));
        }

        MonoString* GetBehaviorValue(uint64_t id, MonoString* key)
        {
            ActorBehavior* behavior = Behavior(id);
            if (!behavior || !key) return nullptr;
            const auto found = behavior->SavedValues.find(Text(key));
            return found == behavior->SavedValues.end() ? nullptr : mono_string_new(mono_domain_get(), found->second.c_str());
        }

        MonoObject* InvokeTask(MonoObject* function, const BehaviorNode& node)
        {
            void* args[] = {mono_string_new(mono_domain_get(), node.Action.c_str())};
            MonoObject* exception = nullptr;
            MonoObject* result = mono_runtime_delegate_invoke(function, args, &exception);
            if (exception) RF_CORE_ERROR("行为树动作 {}：{}", node.Action, mono_class_get_name(mono_object_get_class(exception)));
            return exception ? nullptr : result;
        }

        void HaltBehavior(uint64_t id, MonoObject* abort)
        {
            Entity actor = World().GetEntityByUUID(id);
            if (!actor || !actor.HasComponent<ActorBehavior>()) return;
            ActorBehavior& behavior = actor.GetComponent<ActorBehavior>();
            behavior.Tree.Halt(behavior.Run, [&](const BehaviorNode& node) { if (abort) InvokeTask(abort, node); });
            behavior.Trace = behavior.Run.Trace;
        }

        BehaviorStatus TickBehavior(uint64_t id, MonoArray* conditions, MonoObject* tick, MonoObject* abort)
        {
            ActorBehavior* behavior = Behavior(id);
            if (!behavior || !tick) return BehaviorStatus::Failure;
            Entity actor = World().GetEntityByUUID(id);
            if (behavior->File != actor.GetComponent<BehaviorTreeComponent>().File) HaltBehavior(id, abort);
            if (!OpenBehavior(id, *behavior, conditions) || !behavior->Tree.UsesTasks()) return BehaviorStatus::Failure;

            // 回调只在本帧借用。跨帧保存的是节点编号；下一帧用同一角色的脚本继续。
            const BehaviorStatus result = behavior->Tree.Update(behavior->Facts, behavior->Run,
                [&](const BehaviorNode& node) {
                    MonoObject* answer = InvokeTask(tick, node);
                    return answer ? *static_cast<BehaviorStatus*>(mono_object_unbox(answer)) : BehaviorStatus::Failure;
                },
                [&](const BehaviorNode& node) { if (abort) InvokeTask(abort, node); });
            behavior->Trace = behavior->Run.Trace;
            return result;
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

		int TraverseWithCollider(uint64_t actor, const glm::vec3* delta, float seconds, int input)
		{
			Entity entity = World().GetEntityByUUID(actor);
			if (!entity) return int(TraversalMode::Blocked);
			return Traverse(actor, delta, CharacterRadius(entity), seconds, input);
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
			if (index < 2) rig.Soles[index].clear();
		}

		void AddFootContact(uint64_t actor, int foot, MonoString* bone, const glm::vec3* point)
		{
			Entity entity = World().GetEntityByUUID(actor);
			if (!entity || foot < 0 || foot > 1) return;
			auto& rig = World().GetRegistry().get_or_emplace<TraversalRig>(static_cast<entt::entity>(entity));
			rig.Soles[foot].push_back({Text(bone), *point});
		}

		void SelectClip(uint64_t actor, MonoString* name, bool locomotion)
		{
			Entity entity = World().GetEntityByUUID(actor);
			if (!entity || !entity.HasComponent<ModelAnimationComponent>()) return;
			ModelAnimationComponent& animation = entity.GetComponent<ModelAnimationComponent>();
			const std::string clip = Text(name);
			auto* animator = World().GetRegistry().try_get<ActorAnimator>(static_cast<entt::entity>(entity));
			if (animator && animator->Clip != clip) animator->Timeline.Cancel();
			if (animation.Clip != clip && animator && animation.Clip == animator->Clip) animation.Loop = true;
			animation.PlayClip(clip, animator ? &animator->Controller : nullptr);
			animation.MatchTravel = locomotion;
			if (locomotion) animation.Loop = true;
		}

		void PlayClip(uint64_t actor, MonoString* name) { SelectClip(actor, name, false); }
		void PlayLocomotion(uint64_t actor, MonoString* name) { SelectClip(actor, name, true); }

		float ClipDuration(uint64_t id, MonoString* name)
		{
			Entity actor = World().GetEntityByUUID(id);
			if (!actor || !actor.HasComponent<ModelAnimationComponent>()) return 0.f;
			const Ref<ModelAnimation>& animation = actor.GetComponent<ModelAnimationComponent>().GetAnimation();
			const std::string requested = Text(name);
			if (animation)
				for (const ModelAnimation::Clip& clip : animation->GetClips())
					if (clip.Name == requested) return clip.Duration;
			return 0.f;
		}

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
			const HandContact* contact = World().GetRegistry().try_get<HandContact>(entt::entity(entity));
			if (contact && contact->Action == "heal") CommitContact(World(), actor);
			return true;
		}

		bool Hit(uint64_t source, uint64_t target, float range, float amount)
		{
			return CanReach(World(),source,target,range) && ApplyDamage(World(),source,target,amount);
		}

		bool ApproachHand(uint64_t actor, uint64_t target, MonoString* action, float seconds) { return ApproachContact(World(),actor,target,Text(action),seconds); }
		bool BeginHand(uint64_t actor, uint64_t target, MonoString* action) { return BeginContact(World(),actor,target,Text(action)); }
		bool HandTouches(uint64_t actor) { return AtContact(World(),actor); }
		void EndHand(uint64_t actor) { EndContact(World(),actor); }

		bool CanCollect(uint64_t actor, uint64_t target, float range)
		{
			return CheckPickup(World(),actor,target,range) == ActionResult::Completed;
		}

		bool Collect(uint64_t actor, uint64_t target, float range)
		{
			return CollectItem(World(),actor,target,range) == ActionResult::Completed;
		}

		bool Give(uint64_t actor, uint64_t recipient, MonoString* item, int count, float range)
		{
			return GiveItem(World(),actor,recipient,Text(item),count,range) == ActionResult::Completed;
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
			if (!entity || height <= 0.f) return;
			const glm::vec3 scale = entity.GetComponent<TransformComponent>().Scale;
			if (scale.y == 0.f) return;

			// 蹲下时只降低顶部，脚底的位置与站立时相同。
			if (entity.HasComponent<CapsuleCollider3DComponent>()) {
				auto& capsule = entity.GetComponent<CapsuleCollider3DComponent>();
				if (capsule.Axis != CapsuleAxis::Y) return;
				const float previous = capsule.GetWorldHeight(scale);
				capsule.Height = glm::max(height / std::abs(scale.y), 2.f * capsule.Radius);
				capsule.Center.y += .5f * (capsule.GetWorldHeight(scale) - previous) / scale.y;
			} else if (entity.HasComponent<BoxCollider3DComponent>()) {
				auto& box = entity.GetComponent<BoxCollider3DComponent>();
				const float previous = box.HalfSize.y;
				box.HalfSize.y = .5f * height / std::abs(scale.y);
				box.Center.y += std::copysign(1.f, scale.y) * (box.HalfSize.y - previous);
			}
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
		mono_add_internal_call("Redflag.GameWorld::ChooseBehavior", (const void*)ChooseBehavior);
		mono_add_internal_call("Redflag.GameWorld::TickBehavior", (const void*)TickBehavior);
		mono_add_internal_call("Redflag.GameWorld::HaltBehavior", (const void*)HaltBehavior);
		mono_add_internal_call("Redflag.GameWorld::SetBehaviorValue", (const void*)SetBehaviorValue);
		mono_add_internal_call("Redflag.GameWorld::GetBehaviorValue", (const void*)GetBehaviorValue);
		mono_add_internal_call("Redflag.GameWorld::Move", (const void*)Move);
		mono_add_internal_call("Redflag.GameWorld::Traverse", (const void*)Traverse);
		mono_add_internal_call("Redflag.GameWorld::TraverseWithCollider", (const void*)TraverseWithCollider);
		mono_add_internal_call("Redflag.GameWorld::CancelTraversal", (const void*)StopTraversal);
		mono_add_internal_call("Redflag.GameWorld::SetTraversalLimb", (const void*)SetTraversalLimb);
		mono_add_internal_call("Redflag.GameWorld::AddFootContact", (const void*)AddFootContact);
		mono_add_internal_call("Redflag.GameWorld::SetMovementRoot", (const void*)SetMovementRoot);
		mono_add_internal_call("Redflag.GameWorld::PlayClip", (const void*)PlayClip);
		mono_add_internal_call("Redflag.GameWorld::ClipDuration", (const void*)ClipDuration);
		mono_add_internal_call("Redflag.GameWorld::PlayLocomotion", (const void*)PlayLocomotion);
		mono_add_internal_call("Redflag.GameWorld::Pose", (const void*)HoldPose);
		mono_add_internal_call("Redflag.GameWorld::MaxHealth", (const void*)MaxHealth);
		mono_add_internal_call("Redflag.GameWorld::Health", (const void*)Health);
		mono_add_internal_call("Redflag.GameWorld::ItemCount", (const void*)ItemCount);
		mono_add_internal_call("Redflag.GameWorld::Heal", (const void*)Heal);
		mono_add_internal_call("Redflag.GameWorld::Hit", (const void*)Hit);
		mono_add_internal_call("Redflag.GameWorld::ApproachContact", (const void*)ApproachHand);
		mono_add_internal_call("Redflag.GameWorld::BeginContact", (const void*)BeginHand);
		mono_add_internal_call("Redflag.GameWorld::AtContact", (const void*)HandTouches);
		mono_add_internal_call("Redflag.GameWorld::EndContact", (const void*)EndHand);
		mono_add_internal_call("Redflag.GameWorld::CanCollect", (const void*)CanCollect);
		mono_add_internal_call("Redflag.GameWorld::Collect", (const void*)Collect);
		mono_add_internal_call("Redflag.GameWorld::Give", (const void*)Give);
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
