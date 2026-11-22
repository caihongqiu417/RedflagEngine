#include "Redflag/AI/SpeechInput.h"
#include <fstream>
#include <iostream>
#include <thread>

using namespace Redflag;

static SpeechEvent Wait(SpeechInput& client)
{
	auto end = std::chrono::steady_clock::now() + std::chrono::seconds(20);
	while (std::chrono::steady_clock::now() < end) {
		if (std::optional<SpeechEvent> event = client.Take()) return *event;
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	throw std::runtime_error("transcript timed out");
}

int main(int argc, char** argv)
{
	if (argc != 3) return 2;
	SpeechInput client(uint16_t(std::stoi(argv[1])));
	std::ifstream file(argv[2],std::ios::binary);
	std::string wav((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
	if (wav.empty()) return 2;
	try {
		client.OpenRoom();
		SpeechEvent room = Wait(client);
		if (room.Type != SpeechEventType::RoomOpened) return 1;
		std::cout << room.Text << '\n';

		// 切换角色时，上一份录音即使识别完也不能再触发游戏动作。
		client.SubmitWav(wav);
		client.Reset();
		client.SubmitWav("invalid wav");
		if (Wait(client).Type != SpeechEventType::Error) return 1;
		client.SubmitWav(wav);
		SpeechEvent answer = Wait(client);
		if (answer.Type != SpeechEventType::Transcript || answer.Text.find("法国") == std::string::npos) return 1;
		std::this_thread::sleep_for(std::chrono::seconds(1));
		std::cout << answer.Text << '\n';
		return client.Take() ? 1 : 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
