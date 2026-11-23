#pragma once

#include "DecisionClient.h"
#include "Redflag/Core/Core.h"
#include "NpcQuery.h"
#include "Redflag/Gameplay/WorldActions.h"
#include "Redflag/Animation/AnimationTimeline.h"
#include <deque>
#include <chrono>
#include <functional>

namespace Redflag
{
	class Scene;
	class NpcScriptAction;

	struct NpcEvent
	{
		double Time;
		std::string Kind, Text;
		uint64_t Target;
		std::string Speech;
		uint64_t Sequence;
	};

	struct NpcWatch
	{
		float NextScan = 0.f;
		uint64_t Request = 0, Target = 0;
		std::map<uint64_t, std::string> SeenClips; // 同一物体保持这个动作时只问一次。
	};

	/* 玩家说话时，角色可以一边继续手上的事，一边等待模型回答。 */
	struct NpcTurn
	{
		uint64_t Request = 0;
		uint64_t PlayerEvent = 0;
		std::string Text;
		bool Writing = false;
	};

	/* Forge 按请求编号查看当时的候选、模型回答，以及游戏实际选中的动作和目标。 */
	struct NpcDecisionTrace
	{
		uint64_t ID = 0, Target = 0;
		std::string Kind, Status, Input, Output, ModelInput, ModelOutput;
		std::string Choice, Intent, Action, Outcome;
		std::vector<DecisionChoice> Candidates;
	};

	/* 每个角色各自保存模型正在回答的问题、执行中的动作和游戏回执。角色定义可以共享。 */
	struct NpcState
	{
		BehaviorBlackboard Blackboard;
		NpcMemory Memory;
		NpcWatch Watch;
		NpcTurn Turn;
		BehaviorTrace Trace;
		std::vector<NpcCandidate> Candidates;
		std::deque<NpcEvent> Events;
		std::deque<NpcDecisionTrace> Decisions;
		uint64_t ActiveDecision = 0;
		NpcCandidate Goal{};
		Interaction Use;
		AnimationTimeline ActionEvents;
		std::string MontageSlot;
		uint64_t MontageCursor = 0;
		Ref<NpcScriptAction> Script;
		uint64_t Request = 0;
		uint64_t Look = 0;
		std::string Instruction, Action;
		std::string Stage;
		float PhaseTime = 0.0f;
		std::optional<glm::vec3> WalkCorner;
		bool ContactMade = false, ContactPending = false, Down = false;
		bool Started = false, LeaderDown = false;
	};

	/* 一局游戏拥有一份 NpcSystem。网络线程交回候选编号，只有场景线程会改动物体和库存。 */
	class NpcSystem
	{
	public:
		explicit NpcSystem(Scene& scene, uint16_t port = 0);
		~NpcSystem();

		/// <summary>玩家输入或说出一句话。闲聊时保留当前动作，听到新指令才换任务。</summary>
		bool Say(uint64_t actor, const std::string& text);

		/// <summary>游戏已确定这是一条指令。先停止旧动作，再让模型从当前可做的事里选择。</summary>
		bool Submit(uint64_t actor, const std::string& text, std::vector<DecisionImage> images = {});
		bool Cancel(uint64_t actor);
		// 场景每帧调用。先接收已返回的模型选择，再推进该角色的行走、交互或观察。
		void Update(float seconds);

		// 宿主可接入自己的相机。拍照与模型回答之间允许玩家改令，用本次观察编号认回结果。
		std::function<bool(uint64_t actor, uint64_t look)> TakeLook;
		void FinishLook(uint64_t actor, uint64_t look, const std::string& observation, bool receivedImage = true);

		/// <param name="cursor">每个读者为每个角色保存一个编号。初值为零，读到事件时会前进。</param>
		const NpcEvent* NextEvent(uint64_t actor, uint64_t& cursor) const;
		const NpcState* Inspect(uint64_t actor) const;
		/// <summary>Forge 开启后保留各角色接下来的模型请求。独立游戏默认关闭。</summary>
		void SetDecisionTracing(bool enabled) { m_TraceDecisions = enabled; }
		NpcDefinition* GetDefinition(uint64_t actor);
		NpcMemory* GetMemory(uint64_t actor);
		bool Report(uint64_t actor, uint64_t target, const std::string& text, const std::string& image = {});
		double SessionSeconds() const;
		std::chrono::steady_clock::time_point SessionStart() const { return m_Start; }

	private:
		uint64_t StartDecision(NpcState& state, const char* kind, DecisionRequest request, bool reply = false, uint64_t target = 0);
		std::optional<DecisionResult> TakeDecision(NpcState& state, uint64_t id);
		void CancelDecision(NpcState& state, uint64_t id);
		static NpcDecisionTrace* FindDecision(NpcState& state, uint64_t id);
		void AddObservations(uint64_t actor, const NpcDefinition& role, const NpcState& state, DecisionRequest& request);
		void UpdateTurn(uint64_t actor, NpcState& state);
		void Reply(uint64_t actor, NpcState& state, const std::string& guidance, const std::string& topic = "chat");
		void CancelTurn(NpcState& state);
		void Ask(uint64_t actor, NpcState& state, const std::string& event, std::vector<DecisionImage> images = {}, uint32_t selector = 0);
		bool BeginRoutine(uint64_t actor, NpcState& state, const std::string& intent, const std::string& event);
		void TargetLost(uint64_t actor, NpcState& state, NpcCandidate lost);
		void Select(uint64_t actor, NpcState& state, const NpcCandidate& choice, uint64_t request = 0);
		// 同一种动作可以有几个候选。用提问时的物体或线索编号认回目标，再沿该角色的行为树选择执行方法。
		void Accept(uint64_t actor, NpcState& state, const DecisionResult& result);
		void Execute(uint64_t actor, NpcState& state, float seconds);
		void FollowClue(uint64_t actor, NpcState& state, float seconds);
		void Observe(uint64_t actor, NpcState& state, const NpcDefinition& role, float seconds);
		void Stop(uint64_t actor, NpcState& state, bool cancelTraversal = true);
		/// <summary>让角色朝目标走一步。目标在交互距离内且两者之间没有遮挡时停步。</summary>
		/// <param name="range">这次动作允许的距离，单位为米。</param>
		/// <returns>此时已经够到目标才返回 true。还在走或被墙挡住时返回 false。</returns>
		bool Approach(uint64_t actor, NpcState& state, float range, float seconds);
		void WalkTo(uint64_t actor, NpcState& state, glm::vec3 place, float stopDistance, float seconds);
		void Enter(uint64_t actor, NpcState& state, const std::string& stage);
		bool Face(uint64_t actor, glm::vec3 position, float seconds);
		void Finish(uint64_t actor, NpcState& state, ActionResult result);
		void Record(NpcState& state, const std::string& kind, const std::string& text, uint64_t target = 0, const std::string& speech = {});

		Scene& m_Scene;
		bool m_TraceDecisions = false;
		DecisionClient m_Client;
		std::map<uint64_t, NpcState> m_Actors;
		std::map<std::string, NpcDefinition> m_Definitions;
		std::vector<uint64_t> m_Cancelled;
		std::string m_LoadError;
		std::chrono::steady_clock::time_point m_Start;
		uint64_t m_NextEvent = 1;
		uint64_t m_NextLook = 1;
	};
}
