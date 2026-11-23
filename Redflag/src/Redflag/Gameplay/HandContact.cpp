#include "rfpch.h"
#include "HandContact.h"
#include "TraversalRig.h"
#include "Redflag/AI/NpcSystem.h"
#include "Redflag/Animation/ActorAnimator.h"
#include "TerrainTraversal.h"
#include "Redflag/Assets/AssetManager.h"
#include "Redflag/Renderer/Model.h"
#include "Redflag/Scene/Entity.h"

namespace Redflag
{
	namespace
	{
        // 原片收手约需四分之一秒。手臂、药盒的旋转和归位共用这段过渡。
        constexpr float HandReleaseSeconds = .28f;

		const ContactSocket* Socket(Entity entity, const std::string& name)
		{
			if (!entity || !entity.HasComponent<ContactSocketsComponent>()) return nullptr;
			const auto& points = entity.GetComponent<ContactSocketsComponent>().Points;
			const auto found = points.find(name);
			return found != points.end() ? &found->second : nullptr;
		}

		glm::vec3 Flat(glm::vec3 p) { p.y = 0.f; return p; }

		const AnimationState* ContactClip(Scene& scene, Entity owner, const std::string& action)
		{
			if (owner.HasComponent<ActorAnimator>()) return owner.GetComponent<ActorAnimator>().Controller.Find(action);
			if (owner.HasComponent<NpcComponent>() && scene.GetNpcs())
				if (const auto* role=scene.GetNpcs()->GetDefinition(owner.GetUUID())) return role->Controller.Find(action);
			return nullptr;
		}

		glm::vec3 AuthoredReach(Scene& scene, Entity owner, const std::string& action)
		{
			const auto* state=ContactClip(scene,owner,action);
			const auto* grip=Socket(owner,"grip");
			if (!state || !grip || !owner.HasComponent<ModelAnimationComponent>()) return {0,0,.45f};
			const auto& animation = owner.GetComponent<ModelAnimationComponent>().GetAnimation();
			if (!animation) return {0,0,.45f};
			const auto& clips=animation->GetClips();
			const auto clip=std::find_if(clips.begin(),clips.end(),[&](const auto& c){return c.Name==state->Clip;});
			if (clip==clips.end()) return {0,0,.45f};
			std::vector<ModelAnimation::NodePose> pose;
			std::vector<glm::mat4> bones;
			animation->SamplePose(clip-clips.begin(),clip->Duration*(state->HasContact ? state->Contact : .5f),pose);
			animation->GetModelTransforms(pose,bones);
			glm::mat4 finger;
			if (!animation->GetNodeTransform(grip->Bone,bones,finger)) return {0,0,.45f};
			return glm::vec3(finger*glm::vec4(grip->Point,1))*owner.GetComponent<TransformComponent>().Scale;
		}

		bool Goal(Entity owner, Entity target, const std::string& action, glm::vec3& point)
		{
			if (action == "receive" || action == "give") {
				glm::vec3 a, b;
				if (!ReadContactSocket(owner,"offer",a) || !ReadContactSocket(target,"offer",b)) return false;
				point = .5f * (a+b);
				return true;
			}
			return ReadContactSocket(target,action,point);
		}

		void ReleasePair(Scene& scene, Entity owner)
		{
			HandContact& hand = owner.GetComponent<HandContact>();
			hand.Released = true;
			Entity partner = scene.GetEntityByUUID(hand.Partner);
			if (partner && partner.HasComponent<HandContact>()) {
				auto& other = partner.GetComponent<HandContact>();
				if (other.Partner == uint64_t(owner.GetUUID())) other.Released = true;
			}
		}

		uint64_t MakeProp(Scene& scene, Entity owner, glm::vec3 place)
		{
			const std::string& file = owner.GetComponent<ContactSocketsComponent>().ItemModel;
			if (file.empty()) return 0;
			Entity prop = scene.CreateEntity("手持物品");
			prop.AddComponent<HandProp>();
			auto& model = prop.AddComponent<ModelRendererComponent>();
			model.FilePath = file;
			model.ModelID = AssetManager::Add<Model>(file);
			prop.GetComponent<TransformComponent>().Translation = place;
			return prop.GetUUID();
		}
	}

	bool ReadContactSocket(Entity entity, const std::string& name, glm::vec3& point)
	{
		const ContactSocket* s = Socket(entity,name);
		if (!s) return false;
		glm::mat4 bone(1);
		if (!s->Bone.empty() && (!entity.HasComponent<ModelAnimationComponent>() ||
			!entity.GetComponent<ModelAnimationComponent>().GetNodeTransform(s->Bone,bone))) return false;
		point = entity.GetComponent<TransformComponent>().GetTransform() * bone * glm::vec4(s->Point,1);
		return true;
	}

	bool UsesHandContact(Entity actor, Entity target, const std::string& action)
	{
		return Socket(actor,"grip") && (action == "give" ? Socket(target,"grip") && Socket(target,"offer") :
			Socket(target,action) != nullptr);
	}

	bool ApproachContact(Scene& scene, uint64_t actor, uint64_t target, const std::string& action, float seconds)
	{
		Entity owner = scene.GetEntityByUUID(actor), object = scene.GetEntityByUUID(target);
		glm::vec3 goal;
		if (!owner || !object || !Goal(owner,object,action,goal)) return false;
		if (actor == target) return true;
		auto& tr = owner.GetComponent<TransformComponent>();
		const glm::mat4 objectModel = object.GetComponent<TransformComponent>().GetTransform();
		const auto* s = Socket(object,action == "give" ? "offer" : action);
		glm::vec3 away = Flat(tr.Translation-goal);
		if (glm::length(away) < .001f) away = {0,0,-1};
		const glm::vec3 authored = AuthoredReach(scene,owner,action);
		glm::vec3 mark = goal + glm::normalize(away) * glm::length(Flat(authored));
		if (action == "give") {
			// 以队友身体为圆心，给两人的前臂各留一段可以弯曲的距离。
			const glm::vec3 center = glm::vec3(objectModel[3]);
			away = Flat(tr.Translation-center);
			mark = center + (glm::length(away) > .001f ? glm::normalize(away) : glm::vec3(0,0,-1)) * owner.GetComponent<ContactSocketsComponent>().GiveDistance;
		} else if (s && glm::length(Flat(s->Stand)) > .001f) {
			mark = goal + glm::vec3(objectModel * glm::vec4(s->Stand,0));
		}
		const float radius = owner.HasComponent<BoxCollider3DComponent>() ?
			owner.GetComponent<BoxCollider3DComponent>().HalfSize.x * std::abs(tr.Scale.x) : .3f;
		const glm::vec3 finalMark = mark;
		if (s && glm::length(Flat(s->Stand))>.001f) mark = NextBoxCorner(object,tr.Translation,mark,radius);
		glm::vec3 offset = Flat(mark-tr.Translation);
		const float remaining = glm::length(offset);
		const glm::vec3 face = remaining > .12f ? offset : Flat(goal-tr.Translation);
		const float yaw = std::atan2(face.x,face.z) - (remaining <= .12f && action != "give" ? std::atan2(authored.x,authored.z) : 0.f);
		const float turn = std::remainder(yaw-tr.Rotation.y, glm::two_pi<float>());
		tr.Rotation.y += glm::clamp(turn,-5.f*seconds,5.f*seconds);
		if (glm::length(Flat(finalMark-tr.Translation)) <= .035f) return std::abs(turn) <= .10f;
		if (remaining < .001f) return false;
		MoveCharacter(scene,actor,offset * (std::min(remaining,1.7f*seconds)/remaining),radius,seconds);
		return false;
	}

	bool BeginContact(Scene& scene, uint64_t actor, uint64_t target, const std::string& action)
	{
		Entity owner = scene.GetEntityByUUID(actor), object = scene.GetEntityByUUID(target);
		glm::vec3 point, grip;
		if (!UsesHandContact(owner,object,action) || !Goal(owner,object,action,point) ||
			!ReadContactSocket(owner,"grip",grip)) return false;
		auto& registry = scene.GetRegistry();
		auto available = [&](Entity who) {
			const auto* hand = registry.try_get<HandContact>(static_cast<entt::entity>(who));
			return !hand || hand->Released;
		};
		if (!available(owner) || (action == "give" && !available(object))) return false;
		auto start = [&](Entity who, Entity other, const std::string& kind) -> HandContact& {
			if (who.HasComponent<HandContact>()) {
				Entity prop = scene.GetEntityByUUID(who.GetComponent<HandContact>().Prop);
				if (prop) scene.DestroyEntity(prop);
			}
			HandContact& hand = registry.emplace_or_replace<HandContact>(static_cast<entt::entity>(who));
			hand.Target = other.GetUUID();
			hand.Action = kind;
			hand.Goal = point;
			return hand;
		};
		HandContact& hand = start(owner,object,action);
		const AnimationState* clip = ContactClip(scene,owner,action);
		if (action == "pickup") hand.PropGrip = Socket(object,action)->Point;
		hand.Timed = clip && (clip->HasContact || action == "revive" || action == "repair");
		if (clip && clip->Seconds > 0.f)
			hand.ReachAt = clip->Seconds * (clip->HasContact ? clip->Contact : .5f);
		if (action == "give") {
			hand.Partner = target;
			auto& receiving = start(object,owner,"receive");
			receiving.Partner = actor;
			receiving.ReachAt = hand.ReachAt;
		}
		if (action == "give" || action == "heal") hand.Prop = MakeProp(scene,owner,grip);
		return true;
	}

	bool AtContact(Scene& scene, uint64_t actor)
	{
		Entity owner = scene.GetEntityByUUID(actor);
		if (!owner || !owner.HasComponent<HandContact>()) return false;
		const auto& hand = owner.GetComponent<HandContact>();
		Entity target = scene.GetEntityByUUID(hand.Target);
		glm::vec3 goal, grip;
		if (hand.Released || !target || !Goal(owner,target,hand.Action,goal) || !ReadContactSocket(owner,"grip",grip)) return false;
		// 两个人各用自己的握持范围，只有双方都够到药盒才能交接。
		const float tolerance = owner.GetComponent<ContactSocketsComponent>().GripTolerance;
		if (glm::distance(grip,goal) > tolerance) return false;
		if (hand.Action == "give" && (!ReadContactSocket(target,"grip",grip) || glm::distance(grip,goal)>target.GetComponent<ContactSocketsComponent>().GripTolerance)) return false;
		return true;
	}

	void EndContact(Scene& scene, uint64_t actor)
	{
		Entity owner = scene.GetEntityByUUID(actor);
		if (owner && owner.HasComponent<HandContact>()) ReleasePair(scene,owner);
	}

	void CommitContact(Scene& scene, uint64_t actor)
	{
		Entity owner = scene.GetEntityByUUID(actor);
		if (!owner || !owner.HasComponent<HandContact>()) return;
		auto& hand = owner.GetComponent<HandContact>();
		if (hand.Passed) return;
		Entity target = scene.GetEntityByUUID(hand.Target);
		Goal(owner,target,hand.Action,hand.CommitGoal);
		ReadContactSocket(owner,"grip",hand.CommitHand);
		hand.Goal = hand.CommitGoal;
		hand.Passed = true;
		hand.ShowContact = true;
		if (hand.Action == "pickup") hand.Prop = MakeProp(scene,owner,hand.Goal);
		if (hand.Action == "give") {
			Entity other = scene.GetEntityByUUID(hand.Target);
			if (other && other.HasComponent<HandContact>()) {
				auto& receiving = other.GetComponent<HandContact>();
				receiving.Prop = std::exchange(hand.Prop,0);
				receiving.Passed = true;
				receiving.ShowContact = true;
				receiving.Goal = receiving.CommitGoal = hand.CommitGoal;
				ReadContactSocket(other,"grip",receiving.CommitHand);
			}
		}
	}

	void PrepareContacts(Scene& scene, float seconds)
	{
		auto& registry = scene.GetRegistry();
		for (entt::entity id : registry.view<HandContact>()) {
			Entity owner(id,&scene);
			auto& hand = owner.GetComponent<HandContact>();
			if (hand.ShowContact) continue;
			hand.Age += seconds;
			if (!hand.Passed && !hand.Released) {
				Entity object = scene.GetEntityByUUID(hand.Target);
				if (!object || !Goal(owner,object,hand.Action,hand.Goal)) ReleasePair(scene,owner);
			}
			const bool reach = !hand.Released && !hand.Passed;
			// 让原片先带动肩肘。最后约半段伸手才逐渐对准接触点，接物后平滑交还原片。
			hand.Weight = reach ? glm::smoothstep(.45f * hand.ReachAt,hand.ReachAt,hand.Age) :
				std::max(0.f,hand.Weight - seconds/HandReleaseSeconds);
		}
	}

	void AdjustHandPose(Entity owner, const TraversalRig& rig, const HandContact& hand, const ModelAnimation& animation,
		std::vector<ModelAnimation::NodePose>& pose)
	{
		if (hand.Weight <= 0.f) return;
		const auto& limb = rig.Limbs[size_t(TraversalLimb::LeftHand)];
		const ContactSocket* s = Socket(owner,"grip");
		std::vector<glm::mat4> bones;
		animation.GetModelTransforms(pose,bones);
		glm::mat4 wrist, finger;
		if (!s || !animation.GetNodeTransform(limb.Tip,bones,wrist) || !animation.GetNodeTransform(s->Bone,bones,finger)) return;
		const glm::vec3 grip(finger * glm::vec4(s->Point,1));
		glm::vec3 goal(glm::inverse(owner.GetComponent<TransformComponent>().GetTransform()) * glm::vec4(hand.Goal,1));
		if (hand.Action == "heal") {
			const auto* panel = Socket(owner,"heal");
			glm::mat4 torso;
			if (panel && animation.GetNodeTransform(panel->Bone,bones,torso)) goal = torso*glm::vec4(panel->Point,1);
		}
		if (hand.Action == "give" || hand.Action == "receive") {
			// 两人手掌角度各异。手指也是腕部的子节点，把手指设为末端，前臂和手腕能一同转向药包。
			const ModelAnimation::Limb grasp{limb.Upper,limb.Lower,s->Bone};
			animation.SolveLimb(pose,bones,grasp,glm::mix(grip,goal,hand.Weight),false);
			return;
		}
		// 保留原片里手掌的角度。把腕部移动同样的距离，手指握住的那一点才会落在目标上。
		animation.SolveLimb(pose,bones,limb,glm::vec3(wrist[3]) + glm::smoothstep(0.f,1.f,hand.Weight)*(goal-grip),true);
	}

	// 道具的背面朝向胸甲，模型厚度沿局部 Z 轴量。胸骨可能连同角色一起缩放。
	static glm::quat PanelRotation(Entity target, glm::quat carrying)
	{
		const ContactSocket* socket = Socket(target,"heal");
		if (!socket) return carrying;
		glm::mat4 bone(1);
		if (!socket->Bone.empty() && (!target.HasComponent<ModelAnimationComponent>() ||
			!target.GetComponent<ModelAnimationComponent>().GetNodeTransform(socket->Bone,bone))) return carrying;
		const glm::mat3 basis(target.GetComponent<TransformComponent>().GetTransform()*bone);
		const glm::vec3 front = glm::normalize(glm::transpose(glm::inverse(basis))*glm::vec3(0,0,1));
		const glm::vec3 right = glm::normalize(glm::cross(basis[1],front));
		return glm::quat_cast(glm::mat3(right,glm::cross(front,right),front));
	}

	void UpdateHandProps(Scene& scene, float frameSeconds)
	{
		auto& registry = scene.GetRegistry();
		for (entt::entity id : registry.view<HandContact>()) {
			Entity owner(id,&scene);
			auto& hand = owner.GetComponent<HandContact>();
			const float seconds = hand.ShowContact ? 0.f : frameSeconds;
			hand.ShowContact = false;
			Entity prop = scene.GetEntityByUUID(hand.Prop);
			if (prop) {
				glm::vec3 grip, pouch;
				if (ReadContactSocket(owner,"grip",grip)) {
					if ((hand.Action == "give" || hand.Action == "heal") && !hand.Released &&
						ReadContactSocket(owner,"pouch",pouch)) grip = glm::mix(pouch,grip,glm::smoothstep(0.f,.24f,hand.Age));
					if (hand.Released && ReadContactSocket(owner,"pouch",pouch)) {
						hand.Stow = std::min(1.f,hand.Stow + seconds/HandReleaseSeconds);
						grip = glm::mix(grip,pouch,glm::smoothstep(0.f,1.f,hand.Stow));
					}
					auto& tr = prop.GetComponent<TransformComponent>();
					const glm::quat carrying = glm::angleAxis(owner.GetComponent<TransformComponent>().Rotation.y,glm::vec3(0,1,0));
					glm::quat rotation = carrying;
					if (hand.Action == "heal") {
						const float pressed = glm::smoothstep(0.f,hand.ReachAt*.6f,hand.Age)*(1.f-hand.Stow);
						rotation = glm::slerp(carrying,PanelRotation(scene.GetEntityByUUID(hand.Target),carrying),pressed);
					} else if (hand.Action == "receive") {
						// 接手时保留同一个包的朝向，收回腰间时它才转向接收方。
						rotation = glm::quat(tr.Rotation);
						grip = glm::mix(grip,tr.Translation+rotation*hand.PropGrip,hand.Weight);
						if (hand.Released) {
							const float angle = glm::angle(glm::inverse(rotation)*carrying);
							const float remaining = std::min(angle,glm::two_pi<float>()-angle);
							if (remaining>0.f) rotation=glm::slerp(rotation,carrying,std::min(1.f,glm::pi<float>()*seconds/(HandReleaseSeconds*remaining)));
						}
					}
					tr.Rotation = glm::eulerAngles(rotation);
					tr.Translation = grip - rotation*hand.PropGrip;
				}
			}
			if (hand.Released && hand.Weight == 0.f && (!prop || hand.Stow >= 1.f || !Socket(owner,"pouch"))) {
				if (prop) scene.DestroyEntity(prop);
				owner.RemoveComponent<HandContact>();
			}
		}
	}
}
