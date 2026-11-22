#include "Redflag/AI/DecisionClient.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace Redflag;

static DecisionResult Wait(DecisionClient& client, uint64_t id)
{
	auto end = std::chrono::steady_clock::now() + std::chrono::seconds(45);
	while (std::chrono::steady_clock::now() < end) {
		if (std::optional<DecisionResult> result = client.Take(id)) return *result;
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	throw std::runtime_error("request did not finish");
}

int main(int argc, char** argv)
{
	if (argc != 3) return 2;
	DecisionClient client(static_cast<uint16_t>(std::stoi(argv[1])));
	DecisionRequest request;
	request.Instructions = "You are a game shopkeeper.";
	request.Prompt = "A player needs to unlock the storage cabinet. Which item can open it?";
	request.Observations = { { "cabinet", "The storage cabinet is locked." } };
	request.Candidates = { { "mug", "A ceramic drinking cup" }, { "key", "The brass key to the storage cabinet" } };
	try {
		if (std::string(argv[2]) == "failure") {
			DecisionResult invalid = Wait(client, client.Start(request));
			if (invalid.Status != DecisionStatus::Failed || invalid.Error != "invalid_choice") return 1;
		} else {
			uint64_t first = client.Start(request);
			DecisionResult answer = Wait(client, first);
			if (answer.Status != DecisionStatus::Completed || answer.Choice != "key" || client.Take(first)) {
				std::cerr << "choice=" << answer.Choice << " error=" << answer.Error << '\n';
				return 1;
			}
			DecisionRequest prose;
			prose.Prompt = "请只回答汉字「木」。";
			DecisionResult text = Wait(client, client.StartReply(std::move(prose)));
			if (text.Status != DecisionStatus::Completed || text.Text.find("木") == std::string::npos || !text.Choice.empty()) return 1;
		}
		uint64_t cancelled = client.Start(request);
		client.Cancel(cancelled);
		if (Wait(client,cancelled).Status != DecisionStatus::Cancelled) return 1;
		// 离开场景时丢掉该场景的旧请求；下次启动拿到新的请求编号。
		uint64_t stale = client.Start(request);
		client.Clear();
		if (client.Take(stale)) return 1;
		uint64_t next = client.Start(request);
		if (next == stale) return 1;
		client.Cancel(next);
		return Wait(client, next).Status == DecisionStatus::Cancelled ? 0 : 1;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
