#include "rfpch.h"
#include "NpcSystem.h"
#include "Redflag/Gameplay/HandContact.h"
#include "EnvironmentSense.h"
#include "Redflag/Gameplay/ColliderBounds.h"
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

	void NpcSystem::Ask(uint64_t actor, NpcState& state, const std::string& event, std::vector<DecisionImage> images, uint32_t selector)
	{
		NpcDefinition* definition = GetDefinition(actor);
		if (!definition) { Record(state, "配置", m_LoadError); return; }
		if (!selector) state.Trace = {};
		state.Memory.Configure(definition->Memory);
		state.Candidates = FindCandidates(m_Scene, actor, *definition, state.Memory, bool(TakeLook));
		// 当面认出的实体单独记为「现场看见」。照片和转述保留原来的来源。
		if (definition->Memory.Sight)
		{
			std::set<uint64_t> targets;
			for (const NpcCandidate& choice : state.Candidates)
				if (choice.Target) targets.insert(choice.Target);
			for (const NpcEvidence& entry : state.Memory.GetEvidence())
				if (entry.Target && entry.Source != EvidenceSource::Photo && (entry.Source == EvidenceSource::Reported || definition->Memory.KeepSight))
					targets.insert(entry.Target);

			state.Memory.BeginSight();
			const double seconds = SessionSeconds();
			for (uint64_t id : targets)
			{
				if (CanReach(m_Scene, actor, id, definition->Sight))
					state.Memory.See(id, m_Scene.GetEntityByUUID(id).GetName(), seconds);
			}
		}
		if (selector)
		{
			// 子树只提供自己连出的动作。上次提问后物品可能已经交到了别人手里。
			auto& choices = state.Candidates;
			choices.erase(std::remove_if(choices.begin(), choices.end(), [&](const NpcCandidate& choice) {
				return !definition->Tree.Offers(selector, choice.Intent);
			}), choices.end());
			if (choices.empty())
			{
				Ask(actor, state, "这项任务的候选已不可用，请从现在可以做的事情中重选。");
				return;
			}
		}
		if (state.Candidates.empty()) { Record(state, "等待", "当前没有可执行的动作"); return; }

		DecisionRequest request;
		request.Prompt = state.Instruction.empty() ? "根据现在的队伍情况，选一件你应该做的事。" : state.Instruction;
		const BehaviorNode* node = definition->Tree.Find(selector ? selector : definition->Tree.GetRoot());
		if (node && !node->Question.empty())
			request.Prompt += "\n" + node->Question;
		request.Instructions = definition->Instructions;
		request.Model = definition->Model;
		request.Images = std::move(images);
		if (definition->ObserveWorld) request.Observations = SenseEnvironment(m_Scene, actor);
		state.Memory.AddTo(request);
		if (!event.empty()) request.Observations.push_back({"event", event});
		Entity owner = m_Scene.GetEntityByUUID(actor);
		if (!definition->LeaderRelationship.empty())
		{
			Entity leader = m_Scene.GetEntityByUUID(owner.GetComponent<NpcComponent>().Leader);
			if (leader)
				request.Observations.push_back({"与队友" + leader.GetName() + "的约定", definition->LeaderRelationship});
		}
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
		if (selector) Record(state, "子树提问", node->Name + " " + node->Question);
		else Record(state, "提问", event.empty() ? state.Instruction : event);
	}

	void NpcSystem::Stop(uint64_t actor, NpcState& state, bool cancelTraversal)
	{
		if (cancelTraversal) CancelTraversal(m_Scene, actor);
		state.WalkCorner.reset();
		EndContact(m_Scene,actor);
		state.Look = 0;
		if (state.Watch.Request)
		{
			m_Client.Cancel(state.Watch.Request);
			m_Cancelled.push_back(state.Watch.Request);
			state.Watch.Request = 0;
		}
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
		const auto current = FindCandidates(m_Scene, actor, def, state.Memory, bool(TakeLook));
		const auto valid = std::find_if(current.begin(), current.end(), [&](const NpcCandidate& c) {
			return c.Intent == picked->Intent && c.Target == picked->Target && c.Item == picked->Item && c.Note == picked->Note;
		});
		Record(state, "模型选择", picked->ID + " / " + picked->Description, picked->Target);
		if (valid == current.end()) { TargetLost(actor, state, *picked); return; }
		Select(actor, state, *valid);
	}

	// 作者写明的日常工作和紧急救助可以立即开始，玩家发出的自由指令仍由模型选择。
	bool NpcSystem::BeginRoutine(uint64_t actor, NpcState& state, const std::string& intent, const std::string& event)
	{
		if (intent.empty()) return false;
		const NpcDefinition& definition = *GetDefinition(actor);
		const auto choices = FindCandidates(m_Scene, actor, definition, state.Memory, bool(TakeLook));
		const auto choice = std::find_if(choices.begin(), choices.end(), [&](const NpcCandidate& item) { return item.Intent == intent; });
		if (choice == choices.end()) return false;

		state.Trace = {};
		Record(state, "队伍约定", event + " " + choice->Description, choice->Target);
		Select(actor, state, *choice);
		return true;
	}

	void NpcSystem::TargetLost(uint64_t actor, NpcState& state, NpcCandidate lost)
	{
		Stop(actor, state);
		const std::string event = lost.Description + "：原定的目标现在不可用。";
		Record(state, "目标变化", event, lost.Target);
		const NpcDefinition& definition = *GetDefinition(actor);
		if (!definition.MissingTargetIntent.empty() && lost.Target && !m_Scene.GetEntityByUUID(lost.Target))
		{
			const auto choices = FindCandidates(m_Scene, actor, definition, state.Memory, bool(TakeLook));
			const bool hasReplacement = std::any_of(choices.begin(), choices.end(), [&](const NpcCandidate& item) {
				return item.Intent == lost.Intent && item.Item == lost.Item;
			});
			if (!hasReplacement && BeginRoutine(actor, state, definition.MissingTargetIntent, "现场没有同类目标。"))
			{
				state.Instruction.clear();
				return;
			}
		}
		Ask(actor, state, event + "请从现有目标中重新选择。");
	}

	void NpcSystem::Select(uint64_t actor, NpcState& state, const NpcCandidate& choice)
	{
		GetDefinition(actor)->Tree.Advance(state.Blackboard, choice.Intent, state.Trace);
		if (state.Trace.Waiting)
		{
			Ask(actor, state, "已进入子树，请查看现有候选。", {}, state.Trace.Waiting);
			return;
		}
		state.Goal = choice;
		state.Action = state.Trace.Action;
		Record(state, "选择", choice.ID + " → " + state.Action, choice.Target);
		if (state.Action.empty()) Record(state, "行为树", "这个候选还没有接到动作");
	}

	//========== 角色移动与世界回执 ==========

	void NpcSystem::Enter(uint64_t actor, NpcState& state, const std::string& stage)
	{
		if (state.Stage == stage) return;
		state.ContactPending = false;
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
		animation.MatchTravel = stage == "walk" || stage == "run";
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

	bool NpcSystem::Face(uint64_t actor, glm::vec3 position, float seconds)
	{
		auto& transform = m_Scene.GetEntityByUUID(actor).GetComponent<TransformComponent>();
		const glm::vec3 offset = position - transform.Translation;
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
		Entity owner = m_Scene.GetEntityByUUID(actor);
		const auto* motion = m_Scene.GetRegistry().try_get<TerrainTraversal>(static_cast<entt::entity>(owner));
		if ((!motion || !motion->InAir()) && CanReach(m_Scene, actor, state.Goal.Target, range))
		{
			return true;
		}

		const glm::vec3 place = m_Scene.GetEntityByUUID(state.Goal.Target).GetComponent<TransformComponent>().Translation;
		const float height = place.y - owner.GetComponent<TransformComponent>().Translation.y;
		const float stopDistance = std::sqrt(std::max(0.f, range * range - height * height));
		WalkTo(actor, state, place, stopDistance, seconds);
		return false;
	}

	// 小平台和沟沿仍交给已有的跨越动作。挡在路上的角色和高箱体，从碰撞盒边缘绕过去。
	static std::optional<glm::vec3> FollowCorner(Scene& scene, Entity actor, uint64_t leader,
		glm::vec3 place, float radius)
	{
		const glm::vec3 start=actor.GetComponent<TransformComponent>().Translation;
		glm::vec3 mark=place;
		for (int turn=0; turn<2; ++turn) {
			glm::vec3 offset=mark-start;
			offset.y=0;
			const float distance=glm::length(offset);
			if (distance<.04f) break;
			const glm::vec3 dir=offset/distance, side(-dir.z,0,dir.x);
			Entity obstacle;
			float nearest=std::min(distance,4.f);
			for (float width : {0.f,-radius,radius}) {
				const auto hit=scene.Raycast(start+glm::vec3(0,.8f,0)+width*side,dir,nearest,actor.GetUUID());
				if (!hit || hit->Entity==leader) continue;
				Entity other=scene.GetEntityByUUID(hit->Entity);
				if (!other) continue;
				const auto bounds = GetColliderBounds(other);
				if (!bounds) continue;
				if (!other.HasComponent<HealthComponent>() && bounds->Center.y + bounds->HalfSize.y - start.y <= 1.65f)
					continue;
				nearest=hit->Distance;
				obstacle=other;
			}
			if (!obstacle) break;
			const glm::vec3 corner=NextBoxCorner(obstacle,start,mark,radius+.08f);
			if (glm::distance(glm::vec2(corner.x,corner.z),glm::vec2(mark.x,mark.z))<.01f) break;
			mark=corner;
		}
		if (mark==place) return {};
		mark.y=start.y;
		return mark;
	}

	void NpcSystem::WalkTo(uint64_t actor, NpcState& state, glm::vec3 place, float stopDistance, float seconds)
	{
		Entity owner = m_Scene.GetEntityByUUID(actor);
		const TransformComponent& transform = owner.GetComponent<TransformComponent>();
		const float radius = CharacterRadius(owner);
		const bool follow = state.Action == "follow";
		if (follow) {
			if (state.WalkCorner) {
				// 拐角只决定往哪边走，实际高度由地面决定。两者可能相差一个台阶。
				const glm::vec3 remaining = *state.WalkCorner - transform.Translation;
				if (glm::length(glm::vec2(remaining.x, remaining.z)) < .07f) state.WalkCorner.reset();
			}
			if (!state.WalkCorner) state.WalkCorner=FollowCorner(m_Scene,owner,state.Goal.Target,place,radius);
			if (state.WalkCorner) {place=*state.WalkCorner;stopDistance=0.f;}
		}
		glm::vec3 offset = place - transform.Translation;
		offset.y = 0.0f;

		const float distance = glm::length(offset);
		const glm::vec3 direction = distance > 0.f ? offset / distance : glm::vec3(0.f);
		// 停在交互范围内一厘米。只缩短距离的最后一位，在加回十几米的世界坐标时会被舍掉。
		const float remaining = std::max(0.f,distance-std::max(0.f,stopDistance-.01f));
		const float step = std::min(remaining, GetDefinition(actor)->Speed * seconds);
		Face(actor, place, seconds);
		const glm::vec3 forward(std::sin(transform.Rotation.y), 0.0f, std::cos(transform.Rotation.y));
		const glm::vec3 delta = direction * step * std::max(0.0f, glm::dot(forward, direction));
		const TraversalMode mode = MoveCharacter(m_Scene, actor, delta, radius, seconds, state.WalkCorner ? TraversalInput::Walk : TraversalInput::Automatic);
		if (mode==TraversalMode::Blocked || !follow) state.WalkCorner.reset();
		Enter(actor, state, mode == TraversalMode::Jump ? "jump" : mode == TraversalMode::Climb ? "climb" :
			mode == TraversalMode::Blocked ? "blocked" : "walk");
	}

	void NpcSystem::Finish(uint64_t actor, NpcState& state, ActionResult result)
	{
		EndContact(m_Scene,actor);
		const bool success = result == ActionResult::Completed;
		const std::string action = state.Action;
		const NpcDefinition& definition = *GetDefinition(actor);
		const auto intent = std::find_if(definition.Intents.begin(), definition.Intents.end(),
			[&](const NpcIntent& item) { return item.Key == state.Goal.Intent; });
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
		else if (action != "investigate" && !definition.IdleIntent.empty())
		{
			state.Instruction.clear();
			BeginRoutine(actor, state, definition.IdleIntent, "先前的工作已完成。");
		}
	}

	//========== 转告线索与实地查看 ==========

	void NpcSystem::FollowClue(uint64_t actor, NpcState& state, float seconds)
	{
		if (state.Look) return;
		const auto& notes = state.Memory.GetEvidence();
		const auto note = std::find_if(notes.begin(), notes.end(),
			[&](const NpcEvidence& entry) { return entry.Number == state.Goal.Note; });
		if (note == notes.end())
		{
			Finish(actor, state, ActionResult::Unavailable);
			return;
		}

		if (state.Action == "report")
		{
			const std::string speech = (note->Source == EvidenceSource::Photo ? "从刚才的照片看，" : "队友提到，") + note->Text;
			Record(state, "报点", speech, 0, speech);
			Finish(actor, state, ActionResult::Completed);
			return;
		}
		if (state.Action != "investigate" || !state.Goal.Place)
		{
			Finish(actor, state, ActionResult::Unavailable);
			return;
		}

		const glm::vec3 place = *state.Goal.Place;
		const glm::vec3 here = m_Scene.GetEntityByUUID(actor).GetComponent<TransformComponent>().Translation;
		const float distance = glm::length(glm::vec2(place.x - here.x, place.z - here.z));
		const NpcDefinition& definition = *GetDefinition(actor);
		if (distance > definition.SearchRadius)
		{
			WalkTo(actor, state, place, definition.SearchRadius, seconds);
			if (state.Stage != "blocked") return;
		}

		// 遇到墙也可以就地再看。到这里没有更改任何物品的库存或归属。
		state.Memory.MarkVisited(note->Number);
		const std::string outcome = distance <= definition.SearchRadius ? "到了线索提到的地方。" : "通往线索的路上有障碍。";
		Enter(actor, state, "idle");
		Record(state, "查看", outcome);
		state.Look = m_NextLook++;
		if (definition.Memory.Pictures && TakeLook && TakeLook(actor, state.Look)) return;
		FinishLook(actor, state.Look, outcome + "这次没有取得新照片。", false);
	}

	void NpcSystem::FinishLook(uint64_t actor, uint64_t look, const std::string& observation, bool receivedImage)
	{
		const auto found = m_Actors.find(actor);
		if (found == m_Actors.end() || !look || found->second.Look != look) return;
		NpcState& state = found->second;
		state.Look = 0;
		const bool wasLooking = state.Action == "look";
		Record(state, receivedImage ? "复看" : "观察未完成", observation, 0, observation);
		Finish(actor, state, receivedImage ? ActionResult::Completed : ActionResult::Unavailable);
		if (receivedImage && !wasLooking) Ask(actor, state, observation);
	}

	void NpcSystem::Execute(uint64_t actor, NpcState& state, float seconds)
	{
		if (state.Action == "look")
		{
			if (state.Look) return;
			state.Look = m_NextLook++;
			if (TakeLook && TakeLook(actor, state.Look)) return;
			state.Look = 0;
			Finish(actor, state, ActionResult::Unavailable);
			return;
		}
		if (state.Goal.Note)
		{
			FollowClue(actor, state, seconds);
			return;
		}
		Entity target = m_Scene.GetEntityByUUID(state.Goal.Target);
		if (!target && !state.ContactMade)
		{
			TargetLost(actor, state, state.Goal);
			return;
		}

		const NpcDefinition& definition = *GetDefinition(actor);
		if (NpcScriptAction::Exists(state.Action))
		{
			if (!state.Script) {
				// 脚本接手时也记下阶段。用药收手后再次进入 idle，播放器才会切回待机。
				Enter(actor, state, state.Action);
				state.Script = NpcScriptAction::Create(state.Action);
			}
			// 脚本也可以发出新指令。它在 Tick 里改令后，旧返回值不能结束新动作。
			Ref<NpcScriptAction> script = state.Script;
			const ActionResult result = script ? script->Tick(actor, state.Goal.Target, seconds) : ActionResult::Unavailable;
			if (state.Script == script && result != ActionResult::Running) Finish(actor, state, result);
			return;
		}
		if (state.Action == "wait")
		{
			Enter(actor, state, "idle");
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
		Entity owner = m_Scene.GetEntityByUUID(actor);
		const std::string contactName = state.Action == "gather" ? "pickup" :
			state.Action == "reserve" || state.Action == "share" ? "give" : state.Action;
		const bool hasSocket = UsesHandContact(owner,target,contactName);
		auto approach = [&](float range) {
			if (!hasSocket) return Approach(actor,state,range,seconds) && Face(actor,target.GetComponent<TransformComponent>().Translation,seconds);
			if (!CanReach(m_Scene,actor,state.Goal.Target,range)) { Approach(actor,state,range,seconds); return false; }
			if (!ApproachContact(m_Scene,actor,state.Goal.Target,contactName,seconds)) { Enter(actor,state,"walk"); return false; }
			return BeginContact(m_Scene,actor,state.Goal.Target,contactName);
		};
		const bool useTarget = target && target.HasComponent<InteractionComponent>() &&
			target.GetComponent<InteractionComponent>().Action == state.Action;
		if (useTarget)
		{
			if (!state.Use.IsRunning())
			{
				const float range = target.GetComponent<InteractionComponent>().Range;
				if (!approach(range)) return;
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
			if (!approach(definition.Reach)) return;
			Enter(actor, state, stage);
		}

		const AnimationState* animation = definition.Controller.Find(stage);
		const float duration = animation ? animation->Seconds : 0.0f;
		state.PhaseTime += seconds;
		state.ActionEvents.Advance(seconds);
		bool contactDue = state.ContactPending || duration == 0.f;
		AnimationNotice notice;
		while (state.ActionEvents.Read(notice))
			if (notice.Phase == NotifyPhase::Point && notice.Name == "contact") contactDue = true;
		if (!state.ContactMade && contactDue)
		{
			state.ContactPending = true;
			if (hasSocket && !AtContact(m_Scene,actor)) {
				if (state.PhaseTime >= duration) Finish(actor,state,ActionResult::OutOfReach);
				return;
			}
			const ActionResult contact = pickingUp
				? CollectItem(m_Scene, actor, state.Goal.Target, definition.Reach)
				: GiveItem(m_Scene, actor, state.Goal.Target, state.Goal.Item, 1, definition.Reach);
			if (contact != ActionResult::Completed)
			{
				Finish(actor, state, contact);
				return;
			}
			state.ContactPending = false;
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
				if (state.Request || state.Watch.Request || !state.Action.empty()) Stop(id, state, false);
				// 倒地会中断队友任务。已离开地面的队友继续沿原来的轨迹落地。
				MoveCharacter(m_Scene, id, glm::vec3(0), CharacterRadius(owner), seconds);
				if (!state.Down) {
					const auto* role = GetDefinition(id);
					if (role && role->Controller.Find("down")) Enter(id,state,"down");
				}
				state.Down = true;
				state.Started = false;
				continue;
			}
			if (state.Down) {
				const auto* role = GetDefinition(id);
				const auto* rise = role ? role->Controller.Find("getup") : nullptr;
				if (rise && state.Stage != "getup") Enter(id,state,"getup");
				if (rise && owner.HasComponent<ModelAnimationComponent>()) {
					const auto& player = owner.GetComponent<ModelAnimationComponent>();
					if (player.GetAnimation()) {
						const auto& clips = player.GetAnimation()->GetClips();
						const auto clip = std::find_if(clips.begin(), clips.end(), [&](const auto& c){ return c.Name==rise->Clip; });
						if (clip!=clips.end() && player.Time < clip->Duration) continue;
					}
				}
				state.Down = false;
			}
			Entity leader = m_Scene.GetEntityByUUID(owner.GetComponent<NpcComponent>().Leader);
			const bool down = leader && leader.HasComponent<HealthComponent>() && leader.GetComponent<HealthComponent>().Current <= 0.0f;
			if (!state.Started || (down && !state.LeaderDown))
			{
				Stop(id, state);
				const NpcDefinition* definition = GetDefinition(id);
				const std::string event = down ? "队友刚刚倒下了。" : "游戏开始，队友还没有说话。";
				if (!definition || !BeginRoutine(id, state, down ? definition->RescueIntent : definition->IdleIntent, event))
					Ask(id, state, event);
				state.Started = true;
			}
			state.LeaderDown = down;
			if (state.Request)
			{
				if (const auto result = m_Client.Take(state.Request)) Accept(id, state, *result);
			}
			if (!state.Action.empty()) Execute(id, state, seconds);
			if (!down && state.Started)
				if (const NpcDefinition* role = GetDefinition(id); role && (state.Watch.Request || !role->Watch.empty()))
					Observe(id, state, *role, seconds);
		}
	}
}
