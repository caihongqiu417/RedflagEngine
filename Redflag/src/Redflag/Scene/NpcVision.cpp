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

		std::optional<uint64_t> Query(const std::string& scene, const std::string& question, uint16_t port)
		{
			YAML::Emitter json;
			json.SetStringFormat(YAML::DoubleQuoted);
			json << YAML::Flow << YAML::BeginMap << YAML::Key << "scene" << YAML::Value << scene
				<< YAML::Key << "question" << YAML::Value << question << YAML::EndMap;
			std::string body = json.c_str();

			Handle session(WinHttpOpen(L"Redflag", WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0));
			if (!session) throw std::runtime_error("Cannot open NPC vision HTTP session");
			WinHttpSetTimeouts(session.get(), 3000, 3000, 3000, 65000);
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
			YAML::Node entity = YAML::Load(reply)["entity"];
			if (!entity.IsDefined()) throw std::runtime_error("NPC vision response is missing entity");
			return entity.IsNull() ? std::nullopt : std::optional<uint64_t>(entity.as<uint64_t>());
		}
	}

	std::future<std::optional<uint64_t>> AskNpcVision(std::string sceneName, std::string question, uint16_t port)
	{
		// 请求只带走问题和场景名称。关掉面板时可以丢弃结果，不必等模型回答。
		std::packaged_task<std::optional<uint64_t>()> request(
			[scene = std::move(sceneName), text = std::move(question), port] { return Query(scene, text, port); });
		auto answer = request.get_future();
		std::thread(std::move(request)).detach();
		return answer;
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
