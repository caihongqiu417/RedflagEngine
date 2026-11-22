#pragma once

#include "DecisionClient.h"
#include "Redflag/Core/Core.h"
#include "NpcQuery.h"
#include "Redflag/Gameplay/WorldActions.h"
#include "Redflag/Animation/AnimationTimeline.h"
#include <deque>
#include <chrono>

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

	/* 每个角色各自保存模型正在回答的问题、执行中的动作和游戏回执。角色定义可以共享。 */
	struct NpcState
	{
		BehaviorBlackboard Blackboard;
		NpcMemory Memory;
		BehaviorTrace Trace;
		std::vector<NpcCandidate> Candidates;
		std::deque<NpcEvent> Events;
		NpcCandidate Goal{};
		Interaction Use;
		AnimationTimeline ActionEvents;
		Ref<NpcScriptAction> Script;
		uint64_t Request = 0;
		std::string Instruction, Action;
		std::string Stage;
		float PhaseTime = 0.0f;
		bool ContactMade = false;
		bool Started = false, LeaderDown = false;
	};

	/* 一局游戏拥有一份 NpcSystem。网络线程交回候选编号，只有场景线程会改动物体和库存。 */
	class NpcSystem
	{
	public:
		explicit NpcSystem(Scene& scene, uint16_t port = 8716);
		~NpcSystem();

		/// <summary>先停止这个角色的旧动作，再把玩家的新话和角色现在可以做的事交给模型。</summary>
		bool Submit(uint64_t actor, const std::string& text, std::vector<DecisionImage> images = {});
		bool Cancel(uint64_t actor);
		void Update(float seconds);

		/// <param name="cursor">每个读者为每个角色保存一个编号。初值为零，读到事件时会前进。</param>
		const NpcEvent* NextEvent(uint64_t actor, uint64_t& cursor) const;
		const NpcState* Inspect(uint64_t actor) const;
		NpcDefinition* GetDefinition(uint64_t actor);
		NpcMemory* GetMemory(uint64_t actor);
		bool Report(uint64_t actor, uint64_t target, const std::string& text, const std::string& image = {});
		double SessionSeconds() const;
		std::chrono::steady_clock::time_point SessionStart() const { return m_Start; }

	private:
		void Ask(uint64_t actor, NpcState& state, const std::string& event, std::vector<DecisionImage> images = {});
		void Accept(uint64_t actor, NpcState& state, const DecisionResult& result);
		void Execute(uint64_t actor, NpcState& state, float seconds);
		void Stop(uint64_t actor, NpcState& state);
		/// <summary>让角色朝目标走一步。目标在交互距离内且两者之间没有遮挡时停步。</summary>
		/// <param name="range">这次动作允许的距离，单位为米。</param>
		/// <returns>此时已经够到目标才返回 true。还在走或被墙挡住时返回 false。</returns>
		bool Approach(uint64_t actor, NpcState& state, float range, float seconds);
		void Enter(uint64_t actor, NpcState& state, const std::string& stage);
		bool Face(uint64_t actor, uint64_t target, float seconds);
		void Finish(uint64_t actor, NpcState& state, ActionResult result);
		void Record(NpcState& state, const std::string& kind, const std::string& text, uint64_t target = 0, const std::string& speech = {});

		Scene& m_Scene;
		DecisionClient m_Client;
		std::map<uint64_t, NpcState> m_Actors;
		std::map<std::string, NpcDefinition> m_Definitions;
		std::vector<uint64_t> m_Cancelled;
		std::string m_LoadError;
		std::chrono::steady_clock::time_point m_Start;
		uint64_t m_NextEvent = 1;
	};
}
