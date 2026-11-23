#include "rfpch.h"
#include "NpcSystem.h"
#include <yaml-cpp/yaml.h>

namespace Redflag
{
	//========== 玩家交谈 ==========

	static std::vector<DialogueLine> RecentDialogue(const NpcState& state, uint64_t before)
	{
		std::vector<DialogueLine> lines;
		const NpcEvent* answer = nullptr;
		// 只带上最近四次已经说完的来回。改口或等待途中撤回的指令没有答话，不能和下一轮拼在一起。
		for (auto it = state.Events.rbegin(); it != state.Events.rend() && lines.size() < 8; ++it)
		{
			if (it->Sequence >= before) continue;
			if (it->Kind == "对话" || it->Kind == "接令") answer = &*it;
			else if (it->Kind == "玩家" && answer) {
				lines.push_back({"assistant", answer->Speech});
				lines.push_back({"user", it->Text});
				answer = nullptr;
			}
		}
		std::reverse(lines.begin(), lines.end());
		return lines;
	}

	static std::string RecallQuestion(const NpcState& state, const std::string& name)
	{
		std::string prompt;
		for (const DialogueLine& line : RecentDialogue(state,state.Turn.PlayerEvent))
			prompt += (line.Role == "user" ? "玩家" : name) + "说：“" + line.Content + "”\n";
		prompt += "玩家本次原话：“" + state.Turn.Text + "”\n要查：";

		// 小模型容易把“我告诉你我叫什么”读成角色在问自己的名字。保留原话，另给查询补说话人。
		// 引号中的“我”可能是第三个人说的。整段引用原样复制，英文等其他文字也保持原样。
		std::string_view rest = state.Turn.Text;
		const std::pair<std::string_view,std::string_view> quotes[] = {
			{"“","”"},{"‘","’"},{"「","」"},{"『","』"},{"\"","\""},{"'","'"}
		};
		while (!rest.empty()) {
			size_t take = 1;
			for (const auto& [open,close] : quotes)
				if (rest.substr(0,open.size()) == open) {
					const size_t end = rest.find(close,open.size());
					take = end == rest.npos ? rest.size() : end + close.size();
					break;
				}
			const std::string_view pronoun = rest.substr(0,std::string_view("我").size());
			if (take == 1 && (pronoun == "我" || pronoun == "你" || pronoun == "您")) {
				take = pronoun.size();
				prompt.append(pronoun);
				if (rest.substr(take,std::string_view("们").size()) != "们")
					prompt += pronoun == "我" ? "（玩家）" : "（" + name + "）";
			} else prompt.append(rest.substr(0,take));
			rest.remove_prefix(take);
		}
		return prompt + "\n简短答案：";
	}

	bool NpcSystem::Say(uint64_t actor, const std::string& text)
	{
		const NpcDefinition* role = GetDefinition(actor);
		if (!role || text.find_first_not_of(" \t\r\n") == std::string::npos) return false;
		NpcState& state = m_Actors[actor];
		CancelTurn(state);

		DecisionRequest request;
		request.Model = role->Model;
		request.Instructions = "选出答复这句话需要什么。问身边地方、景物和现场印象，要看当前观察。"
			"接着上一轮谈或核对说过的话，要看先前的交谈。要求写作或表演，只用说话就能完成。"
			"请角色递物、走动、等候或操作设备，即使客气地用问句表达，也是 order。";
		request.Prompt = text;
		request.Candidates = {{"chat", "日常聊天、问身份、谈心或知识问答"}, {"recall", "追问前面谈过的事或引用上文"},
			{"scene", "谈此地的印象、可见事物、背包与正在做的工作"}, {"create", "当场讲故事、说笑话、唱词或作其他创作"},
			{"order", "要求角色走动、操作物体或改变当前任务"}};

		state.Turn.Text = text;
		state.Turn.Request = StartDecision(state, "对话意图", std::move(request));
		Record(state, "玩家", text);
		state.Turn.PlayerEvent = state.Events.back().Sequence;
		return true;
	}

	void NpcSystem::Reply(uint64_t actor, NpcState& state, const std::string& guidance, const std::string& topic)
	{
		const NpcDefinition& role = *GetDefinition(actor);
		DecisionRequest request;
		request.Model = role.Model;
		const std::string& character = role.Persona.empty() ? role.Instructions : role.Persona;
		const std::string identity = "你叫" + role.Name + "，" + character;
		request.Instructions = identity + "简单问题只需一句话，答完就停。";
		request.Prompt = state.Turn.Text;
		if (topic == "recall")
		{
			request.Instructions = "你在核对过去的谈话。根据发言原文回答资料问题，只返回答案。答话的角色是" + role.Name + "，" + character;
			request.Prompt = RecallQuestion(state,role.Name);
		}
		else if (topic == "create")
			request.Instructions = "这是" + role.Name + "说给队友听的，" + identity +
				"直接创作用户想听的内容，给出完整短小的作品，不要引言。";
		else if (topic == "scene")
		{
			AddObservations(actor, role, state, request);
			request.Instructions = role.Name + "，" + character + "你在对队友说话，选与问题最相关的一项观察，简短回答。";
			if (!state.Goal.Description.empty()) request.Instructions += "\n当前任务：" + state.Goal.Description;

			YAML::Emitter facts;
			facts.SetStringFormat(YAML::DoubleQuoted);
			facts << YAML::Flow << YAML::BeginSeq;
			for (const DecisionFact& fact : request.Observations) facts << fact.Description;
			facts << YAML::EndSeq;
			request.Prompt += "\n记录：" + std::string(facts.c_str()) + "\n台词（约二十字）：";
		}
		else if (!role.LeaderRelationship.empty()) request.Instructions += "\n" + role.LeaderRelationship;
		if (!guidance.empty()) request.Instructions += "\n" + guidance;

		state.Turn.Writing = true;
		state.Turn.Request = StartDecision(state, "角色答话", std::move(request), true);
	}

	void NpcSystem::UpdateTurn(uint64_t actor, NpcState& state)
	{
		if (!state.Turn.Request) return;
		const uint64_t id = state.Turn.Request;
		const std::optional<DecisionResult> result = TakeDecision(state, id);
		if (!result) return;
		state.Turn.Request = 0;
		NpcDecisionTrace* trace = FindDecision(state, id);

		if (result->Status != DecisionStatus::Completed)
		{
			Record(state, "模型", result->Error);
			state.Turn = {};
		}
		else if (state.Turn.Writing)
		{
			if (trace) trace->Outcome = "台词已交给游戏";
			Record(state, "对话", result->Text, 0, result->Text);
			state.Turn = {};
		}
		else if (result->Choice != "order")
		{
			if (trace) trace->Outcome = "继续原来的动作，准备答话";
			Reply(actor,state,"",result->Choice);
		}
		else
		{
			if (trace) trace->Outcome = "从可以执行的动作里选择";
			// 只有听到新指令才换任务。实际交付物品仍要走到对方面前，由场景确认。
			Stop(actor, state);
			state.Instruction = state.Turn.Text;
			Ask(actor, state, "");
			if (!state.Request) Reply(actor, state, "现在没有能执行的动作。向队友说明这一点。");
		}
	}

	void NpcSystem::CancelTurn(NpcState& state)
	{
		if (state.Turn.Request) CancelDecision(state, state.Turn.Request);
		state.Turn = {};
	}
}
