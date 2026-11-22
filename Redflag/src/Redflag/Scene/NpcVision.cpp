#include "rfpch.h"
#include "NpcVision.h"
#include "Scene.h"
#include "Entity.h"
#include <winhttp.h>
#include <yaml-cpp/yaml.h>

#pragma comment(lib, "winhttp.lib")

namespace Redflag
{
	namespace
	{
		struct CloseHTTP { void operator()(void* handle) const { WinHttpCloseHandle(handle); } };
		using Handle = std::unique_ptr<void, CloseHTTP>;

		NpcVisionCandidate Query(const std::string& scene, const std::string& question, uint16_t port, bool includeRegions)
		{
			YAML::Emitter json;
			json.SetStringFormat(YAML::DoubleQuoted);
			json << YAML::Flow << YAML::BeginMap << YAML::Key << "scene" << YAML::Value << scene
				<< YAML::Key << "question" << YAML::Value << question
				<< YAML::Key << "include_regions" << YAML::Value << includeRegions << YAML::EndMap;
			std::string body = json.c_str();

			Handle session(WinHttpOpen(L"Redflag", WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0));
			if (!session) throw std::runtime_error("Cannot open NPC vision HTTP session");
			// 区域查询可能先选图，再确认框内的画面。图片服务每次最多等模型一分钟。
			WinHttpSetTimeouts(session.get(), 3000, 3000, 3000, includeRegions ? 125000 : 65000);
			Handle host(WinHttpConnect(session.get(), L"127.0.0.1", port, 0));
			Handle request(WinHttpOpenRequest(host.get(), L"POST", L"/candidate", nullptr, nullptr, nullptr, 0));
			if (!request || !WinHttpSendRequest(request.get(), L"Content-Type: application/json", -1L,
				body.data(), static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0) ||
				!WinHttpReceiveResponse(request.get(), nullptr))
				throw std::runtime_error("Cannot reach NPC vision service");

			DWORD status = 0, size = sizeof(status);
			if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
				nullptr, &status, &size, nullptr) || status != 200)
				throw std::runtime_error("NPC vision service returned HTTP " + std::to_string(status));

			char bytes[512];
			std::string reply;
			DWORD count;
			do
			{
				if (!WinHttpReadData(request.get(), bytes, sizeof(bytes), &count))
					throw std::runtime_error("Incomplete NPC vision response");
				reply.append(bytes, count);
			} while (count);
			const YAML::Node payload = YAML::Load(reply);
			const YAML::Node entity = payload["entity"];
			if (!entity.IsDefined()) throw std::runtime_error("NPC vision response is missing entity");
			NpcVisionCandidate result;
			if (!entity.IsNull())
				result.Entity = entity.as<uint64_t>();
			else if (const YAML::Node region = payload["region"])
			{
				auto vector = [](const YAML::Node& v) { return glm::vec3(v[0].as<float>(), v[1].as<float>(), v[2].as<float>()); };
				result.Region = NpcViewRegion{ region["camera"].as<std::string>(), region["id"].as<std::string>(),
					region["label"].as<std::string>(), vector(region["origin"]), vector(region["direction"]) };
			}
			return result;
		}

		template<class Function>
		auto SendAsync(Function query)
		{
			// 只带走问题和场景名称。离开原场景时可以丢弃 future，游戏继续更新。
			std::packaged_task<std::invoke_result_t<Function>()> request(std::move(query));
			auto answer = request.get_future();
			std::thread(std::move(request)).detach();
			return answer;
		}
	}

	std::future<NpcVisionCandidate> AskNpcCandidate(std::string sceneName, std::string question, uint16_t port)
	{
		return SendAsync([scene = std::move(sceneName), text = std::move(question), port] {
			return Query(scene, text, port, true);
		});
	}

	std::future<std::optional<uint64_t>> AskNpcVision(std::string sceneName, std::string question, uint16_t port)
	{
		return SendAsync([scene = std::move(sceneName), text = std::move(question), port] {
			return Query(scene, text, port, false).Entity;
		});
	}

	bool CanPickUp(Scene& scene, uint64_t candidate, glm::vec3 hand, float reach)
	{
		Entity target = scene.GetEntityByUUID(candidate);
		if (!target || !target.HasComponent<PickupComponent>()) return false;

		glm::vec3 direction = target.GetComponent<TransformComponent>().Translation - hand;
		if (direction == glm::vec3(0)) direction.y = 1;
		std::optional<RayHit3D> hit = scene.Raycast(hand, direction, reach);
		return hit && hit->Entity == candidate;
	}
}
