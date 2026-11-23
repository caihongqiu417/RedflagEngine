#include "rfpch.h"
#include "NpcSystem.h"

namespace Redflag
{
	NpcDecisionTrace* NpcSystem::FindDecision(NpcState& state, uint64_t id)
	{
		if (!id) return nullptr;
		for (auto it = state.Decisions.rbegin(); it != state.Decisions.rend(); ++it)
			if (it->ID == id) return &*it;
		return nullptr;
	}

	uint64_t NpcSystem::StartDecision(NpcState& state, const char* kind, DecisionRequest request, bool reply, uint64_t target)
	{
		NpcDecisionTrace* trace = nullptr;
		if (m_TraceDecisions)
		{
			// 面板可以回看最近 24 次请求。更早的模型文字随这一局的记录滚动移除。
			if (state.Decisions.size() == 24) state.Decisions.pop_front();
			trace = &state.Decisions.emplace_back();
			trace->Kind = kind;
			trace->Status = "等待回答";
			trace->Candidates = request.Candidates;
			trace->Target = target;
		}

		std::string* input = trace ? &trace->Input : nullptr;
		const uint64_t id = reply ? m_Client.StartReply(std::move(request), input) : m_Client.Start(std::move(request), input);
		if (trace) trace->ID = id;
		return id;
	}

	std::optional<DecisionResult> NpcSystem::TakeDecision(NpcState& state, uint64_t id)
	{
		std::optional<DecisionResult> result = m_Client.Take(id);
		if (!result) return {};
		if (NpcDecisionTrace* trace = FindDecision(state, id))
		{
			trace->Output = std::move(result->Output);
			trace->ModelInput = std::move(result->ModelInput);
			trace->ModelOutput = std::move(result->ModelOutput);
			trace->Status = result->Status == DecisionStatus::Completed ? "已回答" : "失败";
			trace->Choice = result->Choice;
			trace->Outcome = result->Error;
		}
		return result;
	}

	void NpcSystem::CancelDecision(NpcState& state, uint64_t id)
	{
		if (NpcDecisionTrace* trace = FindDecision(state, id)) trace->Status = "已取消";
		m_Client.Cancel(id);
		m_Cancelled.push_back(id);
	}
}
