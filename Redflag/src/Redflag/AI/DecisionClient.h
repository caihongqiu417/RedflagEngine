#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Redflag
{
	struct DecisionFact
	{
		std::string ID, Description;
	};

	struct DecisionChoice
	{
		std::string ID, Description;
	};

	struct DecisionImage
	{
		std::string ID, MimeType;
		std::vector<uint8_t> Bytes;
	};

	struct DialogueLine
	{
		std::string Role, Content;
	};

	struct DecisionRequest
	{
		std::string Prompt, Instructions, Model;
		std::vector<DecisionFact> Observations;
		std::vector<DecisionChoice> Candidates;
		std::vector<DecisionImage> Images;
		std::vector<DialogueLine> History;
	};

	enum class DecisionStatus { Completed, Cancelled, Failed };

	struct DecisionResult
	{
		DecisionStatus Status;
		std::string Choice, Error, Text;
		std::string Output, ModelInput, ModelOutput;
	};

	/* 一个场景或编辑器工具持有一份客户端。网络线程只读取请求副本，实体由游戏线程复核。 */
	class DecisionClient
	{
	public:
		explicit DecisionClient(uint16_t port = 0);
		~DecisionClient();
		DecisionClient(const DecisionClient&) = delete;
		DecisionClient& operator=(const DecisionClient&) = delete;

		uint64_t Start(DecisionRequest request, std::string* debugInput = nullptr);
		/// <summary>给游戏或编辑器生成文字。回复保存在 Text 中，由调用者决定怎样使用。</summary>
		uint64_t StartReply(DecisionRequest request, std::string* debugInput = nullptr);
		/// <summary>在游戏线程取一次结果。尚未完成或结果已取走时返回空。</summary>
		std::optional<DecisionResult> Take(uint64_t request);
		void Cancel(uint64_t request);
		void Clear();

	private:
		struct Job;
		uint64_t Begin(DecisionRequest request, bool freeText, std::string* debugInput);
		uint16_t m_Port;
		uint64_t m_NextID = 1;
		std::unordered_map<uint64_t, std::unique_ptr<Job>> m_Jobs;
	};
}
