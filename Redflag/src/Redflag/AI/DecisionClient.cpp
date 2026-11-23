#include "rfpch.h"
#include "DecisionClient.h"
#include "LocalService.h"
#include <condition_variable>
#include <thread>

#include <wincrypt.h>
#include <yaml-cpp/yaml.h>

#pragma comment(lib, "crypt32.lib")

namespace Redflag
{
	namespace
	{
		// ---- 请求与图片 ----

		std::string EncodeImage(const std::vector<uint8_t>& bytes)
		{
			if (bytes.empty()) return {};
			const DWORD flags = CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF;
			DWORD length = 0;
			if (!CryptBinaryToStringA(bytes.data(), DWORD(bytes.size()), flags, nullptr, &length))
				throw std::runtime_error("image_encoding_failed");
			std::string encoded(length, '\0');
			if (!CryptBinaryToStringA(bytes.data(), DWORD(bytes.size()), flags, encoded.data(), &length))
				throw std::runtime_error("image_encoding_failed");
			encoded.resize(length);
			return encoded;
		}

		std::string RequestJSON(const DecisionRequest& request, bool trace, bool omitPixels = false)
		{
			YAML::Emitter out;
			out.SetStringFormat(YAML::DoubleQuoted);
			out << YAML::Flow << YAML::BeginMap << YAML::Key << "prompt" << YAML::Value << request.Prompt
				<< YAML::Key << "instructions" << YAML::Value << request.Instructions
				<< YAML::Key << "model" << YAML::Value << request.Model;
			if (trace) out << YAML::Key << "trace_model" << YAML::Value << true;
			auto entries = [&](const char* key, const auto& items) {
				out << YAML::Key << key << YAML::Value << YAML::BeginSeq;
				for (const auto& item : items)
					out << YAML::BeginMap << YAML::Key << "id" << YAML::Value << item.ID
						<< YAML::Key << "description" << YAML::Value << item.Description << YAML::EndMap;
				out << YAML::EndSeq;
			};
			entries("observations", request.Observations);
			entries("candidates", request.Candidates);
			if (!request.History.empty())
			{
				out << YAML::Key << "history" << YAML::Value << YAML::BeginSeq;
				for (const DialogueLine& line : request.History)
					out << YAML::BeginMap << YAML::Key << "role" << YAML::Value << line.Role
						<< YAML::Key << "content" << YAML::Value << line.Content << YAML::EndMap;
				out << YAML::EndSeq;
			}
			out << YAML::Key << "images" << YAML::Value << YAML::BeginSeq;
			for (const DecisionImage& image : request.Images)
				out << YAML::BeginMap << YAML::Key << "id" << YAML::Value << image.ID
					<< YAML::Key << "mime" << YAML::Value << image.MimeType << YAML::Key << "data" << YAML::Value
					<< (omitPixels ? "[图片数据已省略，" + std::to_string(image.Bytes.size()) + " 字节]" : EncodeImage(image.Bytes)) << YAML::EndMap;
			out << YAML::EndSeq << YAML::EndMap;
			return out.c_str();
		}


	}

	// ---- 后台请求 ----
	struct DecisionClient::Job
	{
		std::thread Worker;
		std::mutex Mutex;
		std::condition_variable Wake;
		bool Cancelled = false;
		std::optional<DecisionResult> Result;

		void Run(uint16_t port, DecisionRequest query, bool freeText, bool trace, std::string body)
		{
			DecisionResult result{ DecisionStatus::Cancelled };
			std::string output, modelInput, modelOutput;
			std::unique_ptr<LocalService> service;
			std::wstring path;
			try {
				service = std::make_unique<LocalService>(port);

				std::wstring route = freeText ? L"/v1/replies" : L"/v1/decisions";
				if (body.empty()) body = RequestJSON(query, trace);
				std::string id = service->Send(L"POST", route, body)["id"].as<std::string>();
				if (id.size() != 32 || id.find_first_not_of("0123456789abcdef") != std::string::npos)
					throw std::runtime_error("invalid_service_reply");
				path = route + L"/" + std::wstring(id.begin(), id.end());
				// 给排队和推理共留一分钟。超时后归还服务端任务，游戏可以重新提问。
				auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(1);
				for (;;) {
					std::unique_lock<std::mutex> lock(Mutex);
					if (Wake.wait_for(lock, std::chrono::milliseconds(40), [&] { return Cancelled; })) break;
					lock.unlock();
					if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error("decision_timeout");

					YAML::Node answer;
					if (trace)
					{
						output = service->RequestJSON(L"GET", path);
						answer = YAML::Load(output);
					}
					else answer = service->Send(L"GET", path);
					std::string state = answer["status"].as<std::string>();
					if (state == "pending")
					{
						output.clear();
						continue;
					}
					if (YAML::Node model = answer["model_trace"])
					{
						modelInput = model["input"].as<std::string>("");
						modelOutput = model["output"].as<std::string>("");
					}
					if (state == "completed" && freeText) {
						result = { DecisionStatus::Completed, {}, {}, answer["text"].as<std::string>() };
					} else if (state == "completed") {
						std::string choice = answer["choice"].as<std::string>();
						bool offered = std::any_of(query.Candidates.begin(), query.Candidates.end(),
							[&](const DecisionChoice& item) { return item.ID == choice; });
						if (!offered) throw std::runtime_error("invalid_choice");
						result = { DecisionStatus::Completed, std::move(choice) };
					} else {
						result = { DecisionStatus::Failed, {}, answer["error"].as<std::string>("invalid_service_reply") };
					}
					break;
				}
			} catch (const std::exception& error) {
				result = { DecisionStatus::Failed, {}, error.what() };
			}
			if (!path.empty()) {
				try { service->Send(L"DELETE", path); }
				catch (const std::exception&) { /* 服务断线时由服务端清理已经结束的请求。 */ }
			}

			std::lock_guard<std::mutex> lock(Mutex);
			result.Output = std::move(output);
			result.ModelInput = std::move(modelInput);
			result.ModelOutput = std::move(modelOutput);
			Result = Cancelled ? DecisionResult{ DecisionStatus::Cancelled } : std::move(result);
		}
	};

	// ---- 游戏线程提交与取回 ----
	DecisionClient::DecisionClient(uint16_t port) : m_Port(port ? port : LocalService::DecisionPort()) {}

	DecisionClient::~DecisionClient() { Clear(); }

	uint64_t DecisionClient::Start(DecisionRequest request, std::string* debugInput)
	{
		return Begin(std::move(request), false, debugInput);
	}

	uint64_t DecisionClient::StartReply(DecisionRequest request, std::string* debugInput)
	{
		return Begin(std::move(request), true, debugInput);
	}

	uint64_t DecisionClient::Begin(DecisionRequest request, bool freeText, std::string* debugInput)
	{
		const uint64_t id = m_NextID++;
		std::unique_ptr<Job>& job = m_Jobs[id];
		job = std::make_unique<Job>();
		std::string body;
		if (debugInput)
		{
			// Forge 显示图像编号和原图大小。较大的像素内容只在后台编码一次。
			*debugInput = RequestJSON(request, true, !request.Images.empty());
			if (request.Images.empty()) body = *debugInput;
		}
		job->Worker = std::thread(&Job::Run, job.get(), m_Port, std::move(request), freeText, debugInput != nullptr, std::move(body));
		return id;
	}

	std::optional<DecisionResult> DecisionClient::Take(uint64_t request)
	{
		auto found = m_Jobs.find(request);
		if (found == m_Jobs.end()) return {};
		Job& job = *found->second;
		std::optional<DecisionResult> result;
		{
			std::lock_guard<std::mutex> lock(job.Mutex);
			if (!job.Result) return {};
			// 模型已经回答，但游戏在线程交接前取消了请求。此时只交付取消状态。
			result = job.Cancelled ? DecisionResult{ DecisionStatus::Cancelled } : std::move(*job.Result);
		}
		job.Worker.join();
		m_Jobs.erase(found);
		return result;
	}

	// ---- 取消请求与离开场景 ----
	void DecisionClient::Cancel(uint64_t request)
	{
		auto found = m_Jobs.find(request);
		if (found == m_Jobs.end()) return;
		Job& job = *found->second;
		std::lock_guard<std::mutex> lock(job.Mutex);
		job.Cancelled = true;
		job.Wake.notify_one();
	}

	void DecisionClient::Clear()
	{
		// 先唤醒所有等待中的请求，再等它们归还服务端任务。下一场景取不到这些结果。
		for (const auto& item : m_Jobs) Cancel(item.first);
		for (const auto& item : m_Jobs)
			if (item.second->Worker.joinable()) item.second->Worker.join();
		m_Jobs.clear();
	}
}
