#include "rfpch.h"
#include "NpcSystem.h"
#include "EnvironmentSense.h"
#include "Redflag/Scene/Entity.h"
#include "Redflag/Gameplay/TerrainTraversal.h"
#include "Redflag/Scripting/NpcScriptAction.h"

namespace Redflag
{
	NpcSystem::NpcSystem(Scene& scene, uint16_t port)
		: m_Scene(scene), m_Client(port), m_Start(std::chrono::steady_clock::now()) {}

	NpcSystem::~NpcSystem()
	{
		for (auto& [actor, state] : m_Actors)
			if (state.Script) state.Script->Cancel(actor, state.Goal.Target);
	}

	//========== 角色定义与本局记录 ==========

	NpcDefinition* NpcSystem::GetDefinition(uint64_t actor)
	{
		Entity owner = m_Scene.GetEntityByUUID(actor);
		if (!owner || !owner.HasComponent<NpcComponent>()) return nullptr;
		const std::string& path = owner.GetComponent<NpcComponent>().Definition;
		auto found = m_Definitions.find(path);
		if (found != m_Definitions.end()) return &found->second;

		NpcDefinition definition;
		if (!definition.Load(std::filesystem::u8path(path), m_LoadError)) return nullptr;
		return &m_Definitions.emplace(path, std::move(definition)).first->second;
	}

	const NpcState* NpcSystem::Inspect(uint64_t actor) const
	{
		const auto found = m_Actors.find(actor);
		return found == m_Actors.end() ? nullptr : &found->second;
	}

	NpcMemory* NpcSystem::GetMemory(uint64_t actor)
	{
		const NpcDefinition* definition = GetDefinition(actor);
		if (!definition) return nullptr;
		NpcMemory& memory = m_Actors[actor].Memory;
		memory.Configure(definition->Memory);
		return &memory;
	}

	double NpcSystem::SessionSeconds() const
	{
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - m_Start).count();
	}

	bool NpcSystem::Report(uint64_t actor, uint64_t target, const std::string& text, const std::string& image)
	{
		NpcMemory* memory = GetMemory(actor);
		return memory && memory->Report(target, text, SessionSeconds(), image);
	}

	const NpcEvent* NpcSystem::NextEvent(uint64_t actor, uint64_t& cursor) const
	{
		const NpcState* state = Inspect(actor);
		if (!state) return nullptr;

		for (const NpcEvent& event : state->Events)
		{
			if (event.Sequence <= cursor) continue;
			cursor = event.Sequence;
			return &event;
		}
		return nullptr;
	}

	void NpcSystem::Record(NpcState& state, const std::string& kind, const std::string& text, uint64_t target, const std::string& speech)
	{
		const double time = SessionSeconds();
		state.Events.push_back({time, kind, text, target, speech, m_NextEvent++});
		if (state.Events.size() > 128) state.Events.pop_front(); // 面板只保留最近 128 条，长局不会一直增加内存。
	}

	//========== 玩家改令与模型选择 ==========

	bool NpcSystem::Submit(uint64_t actor, const std::string& text, std::vector<DecisionImage> images)
	{
		if (text.empty() || !GetDefinition(actor)) return false;
		NpcState& state = m_Actors[actor];
		state.Started = true;
		Stop(actor, state);
		state.Instruction = text;
		Record(state, "玩家", text);
		Ask(actor, state, "", std::move(images));
		return state.Request != 0;
	}

	bool NpcSystem::Cancel(uint64_t actor)
	{
		const auto found = m_Actors.find(actor);
		if (found == m_Actors.end()) return false;
		NpcState& state = found->second;
		if (!state.Request && state.Action.empty()) return false;

		Stop(actor, state);
		state.Instruction.clear();
		return true;
	}

	void NpcSystem::Ask(uint64_t actor, NpcState& state, const std::string& event, std::vector<DecisionImage> images)
	{
		NpcDefinition* definition = GetDefinition(actor);
		if (!definition) { Record(state, "配置", m_LoadError); return; }
		state.Candidates = FindCandidates(m_Scene, actor, *definition);
		// 重新检查这次能操作的目标和以前提到的实体。玩家对它的描述不能替代场景里的名字与遮挡检查。
		state.Memory.Configure(definition->Memory);
		if (definition->Memory.Sight)
		{
			std::set<uint64_t> targets;
			for (const NpcCandidate& choice : state.Candidates) targets.insert(choice.Target);
			for (const NpcEvidence& entry : state.Memory.GetEvidence())
				if (entry.Target && (entry.Source == EvidenceSource::Reported || definition->Memory.KeepSight))
					targets.insert(entry.Target);

			state.Memory.BeginSight();
			const double seconds = SessionSeconds();
			for (uint64_t id : targets)
			{
				if (CanReach(m_Scene, actor, id, definition->Sight))
					state.Memory.See(id, m_Scene.GetEntityByUUID(id).GetName(), seconds);
			}
		}
		if (state.Candidates.empty()) { Record(state, "等待", "当前没有可执行的动作"); return; }

		DecisionRequest request;
		request.Prompt = state.Instruction.empty() ? "根据现在的队伍情况，选一件你应该做的事。" : state.Instruction;
		request.Instructions = definition->Instructions;
		request.Model = definition->Model;
		request.Images = std::move(images);
		if (definition->ObserveWorld) request.Observations = SenseEnvironment(m_Scene, actor);
		state.Memory.AddTo(request);
		if (!event.empty()) request.Observations.push_back({"event", event});
		Entity owner = m_Scene.GetEntityByUUID(actor);
		if (owner.HasComponent<InventoryComponent>())
		{
			std::string stock;
			for (const auto& item : owner.GetComponent<InventoryComponent>().Items)
				stock += item.first + " × " + std::to_string(item.second) + " ";
			request.Observations.push_back({"inventory", stock.empty() ? "背包是空的" : stock});
		}
		if (definition->Remember)
			for (const auto& entry : state.Blackboard.Values)
				request.Observations.push_back({entry.first, entry.second});
		for (const NpcCandidate& choice : state.Candidates)
			request.Candidates.push_back({choice.ID, choice.Description});

		state.Request = m_Client.Start(std::move(request));
		Record(state, "提问", event.empty() ? state.Instruction : event);
	}

	void NpcSystem::Stop(uint64_t actor, NpcState& state)
	{
		CancelTraversal(m_Scene, actor);
		if (state.Request)
		{
			Record(state, "中止", "取消尚未回答的请求");
			m_Client.Cancel(state.Request);
			m_Cancelled.push_back(state.Request);
			state.Request = 0;
		}
		if (!state.Action.empty()) Record(state, "中止", state.Action, state.Goal.Target);
		Ref<NpcScriptAction> script = std::move(state.Script);
		const uint64_t target = state.Goal.Target;
		state.Use = {};
		state.Blackboard.Values.erase("reserved");
		state.Action.clear();
		state.Goal = {};
		state.ContactMade = false;
		Enter(actor, state, "idle");
		if (script) script->Cancel(actor, target);
	}

	void NpcSystem::Accept(uint64_t actor, NpcState& state, const DecisionResult& result)
	{
		state.Request = 0;
		if (result.Status != DecisionStatus::Completed) { Record(state, "模型", result.Error); return; }
		const auto picked = std::find_if(state.Candidates.begin(), state.Candidates.end(),
			[&](const NpcCandidate& candidate) { return candidate.ID == result.Choice; });
		if (picked == state.Candidates.end()) { Record(state, "模型", "没有这个候选"); return; }

		// 两个物体可能共用同一种动作。复查原来选中的实体，不能用重新排序后的编号替代它。
		NpcDefinition& def = *GetDefinition(actor);
		const auto current = FindCandidates(m_Scene, actor, def);
		const auto valid = std::find_if(current.begin(), current.end(), [&](const NpcCandidate& c) {
			return c.Intent == picked->Intent && c.Target == picked->Target && c.Item == picked->Item;
		});
		if (valid == current.end()) { Ask(actor, state, "刚才选择的对象已不可用，请重新选择。"); return; }
		state.Goal = *valid;
		state.Trace = def.Tree.Tick(state.Blackboard, picked->Intent);
		state.Action = state.Trace.Action;
		Record(state, "选择", picked->ID + " → " + state.Action, picked->Target);
		if (state.Action.empty()) Record(state, "行为树", "这个候选还没有接到动作");
	}

	//========== 角色移动与世界回执 ==========

	void NpcSystem::Enter(uint64_t actor, NpcState& state, const std::string& stage)
	{
		if (state.Stage == stage) return;
		state.Stage = stage;
		state.PhaseTime = 0.0f;
		state.ActionEvents = {};
		Record(state, "动作", stage, state.Goal.Target);

		NpcDefinition* definition = GetDefinition(actor);
		Entity owner = m_Scene.GetEntityByUUID(actor);
		if (!definition || !owner) return;

		const AnimationState* shot = definition->Controller.Find(stage);
		if (shot && shot->Seconds > 0.f && (stage == "pickup" || stage == "give")) {
			// 旧动作表用 Contact 表示剪辑已播过的比例。复用事件时间轴，在同一时刻实际交接物品。
			AnimationState interaction = *shot;
			interaction.HasContact = true;
			interaction.Loop = false;
			state.ActionEvents.Play(interaction,shot->Seconds);
		}
		if (!owner.HasComponent<ModelAnimationComponent>()) return;
		ModelAnimationComponent& animation = owner.GetComponent<ModelAnimationComponent>();
		const std::string& clip = shot ? shot->Clip : stage == "walk" ? definition->WalkClip : definition->IdleClip;
		if (clip.empty()) return;
		if (shot)
		{
			animation.BlendSeconds = shot->Blend;
			animation.Loop = shot->Loop;
		}
		if (animation.Clip != clip || (shot && !shot->Loop))
		{
			animation.Clip = clip;
			animation.Time = 0.0f;
		}
	}

	bool NpcSystem::Face(uint64_t actor, uint64_t target, float seconds)
	{
		auto& transform = m_Scene.GetEntityByUUID(actor).GetComponent<TransformComponent>();
		const glm::vec3 offset = m_Scene.GetEntityByUUID(target).GetComponent<TransformComponent>().Translation - transform.Translation;
		if (offset.x == 0.f && offset.z == 0.f) return true;

		// 取较短的方向转身。准备递东西时，即使队友在身后也不会瞬间掉头。
		const float angle = std::atan2(offset.x, offset.z);
		const float remaining = std::remainder(angle - transform.Rotation.y, glm::two_pi<float>());
		const float step = glm::radians(GetDefinition(actor)->TurnSpeed) * seconds;
		transform.Rotation.y += glm::clamp(remaining, -step, step);
		return std::abs(remaining) <= step;
	}

	bool NpcSystem::Approach(uint64_t actor, NpcState& state, float range, float seconds)
	{
		NpcDefinition& def = *GetDefinition(actor);
		Entity owner = m_Scene.GetEntityByUUID(actor);
		const auto* motion = m_Scene.GetRegistry().try_get<TerrainTraversal>(static_cast<entt::entity>(owner));
		if ((!motion || !motion->InAir()) && CanReach(m_Scene, actor, state.Goal.Target, range))
		{
			return true;
		}

		// 目标给出行走方向。脚下的坡面、台阶和可以攀上的平台由角色的物理查询决定。
		TransformComponent& transform = owner.GetComponent<TransformComponent>();
		const TransformComponent& target = m_Scene.GetEntityByUUID(state.Goal.Target).GetComponent<TransformComponent>();
		glm::vec3 offset = target.Translation - transform.Translation;
		offset.y = 0.0f;

		const float distance = glm::length(offset);
		const glm::vec3 direction = distance > 0.f ? offset / distance : glm::vec3(0.f);
		// 一帧过长时也只走到交互范围内。目标与角色有高度差时，把允许的距离换算到地面上。
		const float height = target.Translation.y - transform.Translation.y;
		const float reachOnGround = std::sqrt(std::max(0.f, range * range - height * height));
		const float remaining = std::max(0.f, distance - std::nextafter(reachOnGround, 0.f));
		const float step = std::min(remaining, def.Speed * seconds);
		Face(actor, state.Goal.Target, seconds);
		const glm::vec3 forward(std::sin(transform.Rotation.y), 0.0f, std::cos(transform.Rotation.y));
		const float radius = owner.HasComponent<BoxCollider3DComponent>() ?
			owner.GetComponent<BoxCollider3DComponent>().HalfSize.x * std::abs(transform.Scale.x) : .30f;
		const glm::vec3 delta = direction * step * std::max(0.0f, glm::dot(forward, direction));
		const TraversalMode mode = MoveCharacter(m_Scene, actor, delta, radius, seconds, TraversalInput::Automatic);
		Enter(actor, state, mode == TraversalMode::Jump ? "jump" : mode == TraversalMode::Climb ? "climb" :
			mode == TraversalMode::Blocked ? "blocked" : "walk");
		return false;
	}

	void NpcSystem::Finish(uint64_t actor, NpcState& state, ActionResult result)
	{
		const bool success = result == ActionResult::Completed;
		const std::string action = state.Action;
		const NpcDefinition& definition = *GetDefinition(actor);
		const auto intent = std::find_if(definition.Intents.begin(), definition.Intents.end(),
			[&](const NpcIntent& item) { return item.Key == action; });
		const std::string speech = success && intent != definition.Intents.end() ? intent->Reply : "";
		Record(state, success ? "完成" : "失败", action, state.Goal.Target, speech);

		state.Use = {};
		state.Script.reset();
		state.ContactMade = false;
		state.Action.clear();
		state.Blackboard.Values.erase("reserved");
		Enter(actor, state, "idle");

		if (success && action == "gather")
			Ask(actor, state, "物品已经放进背包。请继续处理队友先前的要求。");
		else if (!success)
			Ask(actor, state, "上次动作没有完成，请根据现状重新选择。");
	}

	void NpcSystem::Execute(uint64_t actor, NpcState& state, float seconds)
	{
		Entity target = m_Scene.GetEntityByUUID(state.Goal.Target);
		if (!target && !state.ContactMade)
		{
			Stop(actor, state);
			Ask(actor, state, "目标已从现场消失。");
			return;
		}

		const NpcDefinition& definition = *GetDefinition(actor);
		if (NpcScriptAction::Exists(state.Action))
		{
			if (!state.Script) state.Script = NpcScriptAction::Create(state.Action);
			// 脚本也可以发出新指令。它在 Tick 里改令后，旧返回值不能结束新动作。
			Ref<NpcScriptAction> script = state.Script;
			const ActionResult result = script ? script->Tick(actor, state.Goal.Target, seconds) : ActionResult::Unavailable;
			if (state.Script == script && result != ActionResult::Running) Finish(actor, state, result);
			return;
		}
		if (state.Action == "follow")
		{
			if (Approach(actor, state, definition.Reach, seconds))
				Enter(actor, state, "idle");
			return;
		}

		if (state.Action == "mark")
		{
			Finish(actor, state, ActionResult::Completed);
			return;
		}

		// 救援和维修按目标设定的时间进行。模型可以改主意，已经做完的交互只生效一次。
		const bool useTarget = target && target.HasComponent<InteractionComponent>() &&
			target.GetComponent<InteractionComponent>().Action == state.Action;
		if (useTarget)
		{
			if (!state.Use.IsRunning())
			{
				const float range = target.GetComponent<InteractionComponent>().Range;
				if (!Approach(actor, state, range, seconds) || !Face(actor, state.Goal.Target, seconds)) return;
				Enter(actor, state, state.Action);
				const ActionResult start = state.Use.Start(m_Scene, actor, state.Goal.Target, state.Action);
				if (start != ActionResult::Running)
				{
					Finish(actor, state, start);
					return;
				}
			}
			const ActionResult result = state.Use.Update(m_Scene, seconds);
			if (result != ActionResult::Running) Finish(actor, state, result);
			return;
		}

		// 拾取和交付分为靠近、伸手、接触、收手。接触后该物品可能已经从场景移到库存。
		const bool pickingUp = state.Action == "gather";
		if (!pickingUp && state.Action != "share" && state.Action != "reserve")
		{
			Finish(actor, state, ActionResult::Unavailable);
			return;
		}
		const std::string stage = pickingUp ? "pickup" : "give";
		if (state.Stage != stage)
		{
			if (state.Action == "reserve")
			{
				state.Blackboard.Values["reserved"] = state.Goal.Item;
				if (!CanReach(m_Scene, actor, state.Goal.Target, definition.Reach))
				{
					Enter(actor, state, "wait");
					return;
				}
			}
			else if (!Approach(actor, state, definition.Reach, seconds)) return;

			if (!Face(actor, state.Goal.Target, seconds)) return;
			Enter(actor, state, stage);
		}

		const AnimationState* animation = definition.Controller.Find(stage);
		const float duration = animation ? animation->Seconds : 0.0f;
		state.PhaseTime += seconds;
		state.ActionEvents.Advance(seconds);
		bool contactDue = duration == 0.f;
		AnimationNotice notice;
		while (state.ActionEvents.Read(notice))
			if (notice.Phase == NotifyPhase::Point && notice.Name == "contact") contactDue = true;
		if (!state.ContactMade && contactDue)
		{
			const ActionResult contact = pickingUp
				? CollectItem(m_Scene, actor, state.Goal.Target, definition.Reach)
				: GiveItem(m_Scene, actor, state.Goal.Target, state.Goal.Item, 1, definition.Reach);
			if (contact != ActionResult::Completed)
			{
				Finish(actor, state, contact);
				return;
			}
			state.ContactMade = true;
			Record(state, "库存", pickingUp ? "物品已放入背包" : "物品已交到队友手中", state.Goal.Target);
		}
		if (state.PhaseTime >= duration) Finish(actor, state, ActionResult::Completed);
	}

	void NpcSystem::Update(float seconds)
	{
		// 改令时角色当场停步。后台请求随后退出，在这里取走它的终态，及时回收线程。
		m_Cancelled.erase(std::remove_if(m_Cancelled.begin(), m_Cancelled.end(),
			[this](uint64_t id) { return m_Client.Take(id).has_value(); }), m_Cancelled.end());

		for (auto it = m_Actors.begin(); it != m_Actors.end(); )
		{
			Entity owner = m_Scene.GetEntityByUUID(it->first);
			if (owner && owner.HasComponent<NpcComponent>()) { ++it; continue; }
			Stop(it->first, it->second);
			it = m_Actors.erase(it);
		}
		for (entt::entity handle : m_Scene.GetRegistry().view<NpcComponent>())
		{
			Entity owner(handle, &m_Scene);
			const uint64_t id = owner.GetUUID();
			NpcState& state = m_Actors[id];
			if (owner.HasComponent<HealthComponent>() && owner.GetComponent<HealthComponent>().Current <= 0)
			{
				if (state.Request || !state.Action.empty()) Stop(id, state);
				state.Started = false;
				continue;
			}
			Entity leader = m_Scene.GetEntityByUUID(owner.GetComponent<NpcComponent>().Leader);
			const bool down = leader && leader.HasComponent<HealthComponent>() && leader.GetComponent<HealthComponent>().Current <= 0.0f;
			if (!state.Started || (down && !state.LeaderDown))
			{
				Stop(id, state);
				Ask(id, state, down ? "队友刚刚倒下了。" : "游戏开始，队友还没有说话。");
				state.Started = true;
			}
			state.LeaderDown = down;
			if (state.Request)
			{
				if (const auto result = m_Client.Take(state.Request)) Accept(id, state, *result);
			}
			if (!state.Action.empty()) Execute(id, state, seconds);
		}
	}
}
