#include "Redflag/Network/NetworkScene.h"
#include "Redflag/Scene/Entity.h"

#include <charconv>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string_view>
#include <thread>

using namespace Redflag;
using Clock = std::chrono::steady_clock;

namespace
{
	// 两个进程用同一个编号指认红箱子。地面只参与主机的物理计算。
	constexpr uint64_t Crate = 103;

	void Pump(WorldClient& client)
	{
		if (!client.Update())
			throw std::runtime_error("Lost connection to the world server");
	}

	void DropCrate(WorldClient& host)
	{
		Scene room;
		Entity floor = room.CreateEntity("Floor");
		floor.GetComponent<TransformComponent>().Translation.y = -.5f;
		floor.AddComponent<RigidBody3DComponent>();
		floor.AddComponent<BoxCollider3DComponent>().HalfSize = { 3.f, .5f, 3.f };

		Entity box = room.CreateEntityWithUUID(Crate, "Crate");
		box.GetComponent<TransformComponent>().Translation.y = 3.f;
		box.AddComponent<RigidBody3DComponent>().Type = BodyType3D::Dynamic;
		box.AddComponent<BoxCollider3DComponent>();
		room.OnSimulationStart();

		const std::vector<uint64_t> shared{ Crate };
		constexpr int stepsPerSecond = 60;
		Clock::time_point start = Clock::now();
		for (int step = 0; step < 2 * stepsPerSecond; ++step)
		{
			Pump(host);
			room.OnUpdatePhysics(1.f / stepsPerSecond);
			// 每三次物理更新发送一次位置，每秒最多 20 份。等确认时场景继续模拟。
			if (step % 3 == 0)
				host.Publish(CapturePoses(room, shared));
			std::this_thread::sleep_until(start + std::chrono::duration<double>(double(step + 1) / stepsPerSecond));
		}
		std::printf("HOST %llu %.7f\n", static_cast<unsigned long long>(Crate),
			box.GetComponent<TransformComponent>().Translation.y);
		room.OnSimulationStop();
	}

	void WatchCrate(WorldClient& viewer)
	{
		Scene room;
		NetworkScene incoming(room);
		uint32_t displayed = 0;
		bool sawCrate = false;
		for (;;)
		{
			Pump(viewer);
			if (viewer.Revision() != displayed)
			{
				if (!incoming.Apply(viewer.Poses()))
					throw std::runtime_error("Received an entity ID already owned by this scene");
				displayed = viewer.Revision();
				Entity crate = room.GetEntityByUUID(Crate);
				if (crate)
				{
					std::printf("STATE %u %.7f\n", displayed, crate.GetComponent<TransformComponent>().Translation.y);
					std::fflush(stdout);
					sawCrate = true;
				}
				else if (sawCrate)
					return; // 主机离开，服务端发来的空场景已经移除了箱子。
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
	}
}

int main(int argc, char** argv)
{
	if (argc < 2 || argc > 4 || (std::string_view(argv[1]) != "host" && std::string_view(argv[1]) != "watch"))
	{
		std::fprintf(stderr, "WorldWorkshop host|watch [IPv4] [port]\n");
		return 2;
	}
	uint16_t port = 42017;
	if (argc == 4)
	{
		const std::string_view text(argv[3]);
		const char* end = text.data() + text.size();
		const std::from_chars_result read = std::from_chars(text.data(), end, port);
		if (read.ec != std::errc() || read.ptr != end || !port) return 2;
	}
	try
	{
		const WorldRole role = std::string_view(argv[1]) == "host" ? WorldRole::Host : WorldRole::Viewer;
		WorldClient client;
		if (!client.Connect(argc >= 3 ? argv[2] : "127.0.0.1", port, role))
			throw std::runtime_error("Could not connect to world_server");
		while (!client.Ready())
		{
			Pump(client);
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		std::puts("READY");
		std::fflush(stdout);
		if (role == WorldRole::Host) DropCrate(client);
		else WatchCrate(client);
	}
	catch (const std::exception& error)
	{
		std::fprintf(stderr, "%s\n", error.what());
		return 1;
	}
}
