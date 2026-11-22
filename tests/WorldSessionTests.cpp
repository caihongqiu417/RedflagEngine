#include "rfpch.h"
#include "Redflag/Network/NetworkScene.h"
#include "Redflag/Scene/Scene.h"
#include "Redflag/Scene/Entity.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <thread>

using namespace Redflag;

static void Check(bool passed, const char* message)
{
	if (!passed)
		throw std::runtime_error(message);
}

int main(int argc, char** argv)
{
	if (argc != 3)
		return 2;
	try
	{
		Scene simulation, left, right;
		Entity floor = simulation.CreateEntity("Ground");
		floor.GetComponent<TransformComponent>().Translation.y = -.5f;
		floor.AddComponent<RigidBody3DComponent>();
		floor.AddComponent<BoxCollider3DComponent>().HalfSize = { 5.f, .5f, 5.f };
		Entity flatFloor = simulation.CreateEntity("2D ground");
		flatFloor.GetComponent<TransformComponent>().Translation.y = -.5f;
		flatFloor.AddComponent<RigidBody2DComponent>();
		flatFloor.AddComponent<BoxCollider2DComponent>().Size = { 5.f, .5f };

		Entity box = simulation.CreateEntity("3D crate");
		box.GetComponent<TransformComponent>().Translation.y = 3.f;
		box.AddComponent<RigidBody3DComponent>().Type = BodyType3D::Dynamic;
		box.AddComponent<BoxCollider3DComponent>();
		Entity flat = simulation.CreateEntity("2D box");
		flat.GetComponent<TransformComponent>().Translation = { 2.f, 2.f, 0.f };
		flat.AddComponent<RigidBody2DComponent>().Type = RigidBody2DComponent::BodyType::Dynamic;
		flat.AddComponent<BoxCollider2DComponent>();
		Entity sign = simulation.CreateEntity("Sign");
		sign.GetComponent<TransformComponent>().Translation = { 1.f, 2.f, 3.f };
		sign.GetComponent<TransformComponent>().Rotation = { -.25f, .5f, 1.f };
		const uint64_t boxID = box.GetUUID(), flatID = flat.GetUUID(), signID = sign.GetUUID();
		const std::vector<uint64_t> ids{ boxID, flatID, signID };

		WorldClient host, a, b;
		uint16_t port = uint16_t(std::stoi(argv[1]));
		Check(host.Connect("127.0.0.1", uint16_t(std::stoi(argv[2])), WorldRole::Host), "connect host");
		Check(a.Connect("127.0.0.1", port, WorldRole::Viewer) && b.Connect("127.0.0.1", port, WorldRole::Viewer), "connect both viewers");
		auto wait = [&](auto condition)
		{
			auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			while (!condition())
			{
				Check(host.Update() && a.Update() && b.Update(), "connection dropped");
				Check(std::chrono::steady_clock::now() < deadline, "timed out waiting for a confirmed snapshot");
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		};
		wait([&] { return host.Ready() && a.Revision() && b.Revision(); });
		NetworkScene leftView(left), rightView(right);
		auto publish = [&](uint32_t sequence)
		{
			uint32_t before = a.Revision();
			Check(host.Publish(CapturePoses(simulation, ids)), "publish host poses");
			Check(!host.Publish({}), "second publish waits for acknowledgement");
			wait([&] { return host.LastAcceptedPublish() == sequence && a.Revision() > before && a.Revision() == b.Revision(); });
			Check(a.Revision() == before + 1, "retransmit advanced the server revision twice");
			Check(leftView.Apply(a.Poses()) && rightView.Apply(b.Poses()), "apply confirmed snapshots");
		};
		publish(1);
		Check(!a.Publish({}), "viewer published host state");

		simulation.OnSimulationStart();
		for (int step = 0; step < 180; ++step)
			simulation.OnUpdatePhysics(1.f / 60.f);
		float physxY = box.GetComponent<TransformComponent>().Translation.y;
		float box2dY = flat.GetComponent<TransformComponent>().Translation.y;
		Check(std::abs(physxY - .5f) < .03f && std::abs(box2dY - .5f) < .03f, "both simulated boxes must rest on their floor");
		Check(left.GetEntityByUUID(boxID).GetComponent<TransformComponent>().Translation.y == 3.f, "viewer moved before the update");
		publish(2);
		for (Scene* view : { &left, &right })
		{
			for (uint64_t id : ids)
			{
				Entity replica = view->GetEntityByUUID(id);
				const TransformComponent& actual = replica.GetComponent<TransformComponent>();
				const TransformComponent& expected = simulation.GetEntityByUUID(id).GetComponent<TransformComponent>();
				Check(std::memcmp(&actual.Translation.x, &expected.Translation.x, 3 * sizeof(float)) == 0 &&
					std::memcmp(&actual.Rotation.x, &expected.Rotation.x, 3 * sizeof(float)) == 0, "pose bits differ from physics host");
				Check(!replica.HasComponent<RigidBody3DComponent>() && !replica.HasComponent<RigidBody2DComponent>(), "viewer also simulates the replica");
			}
		}

		simulation.DestroyEntity(sign);
		publish(3);
		Check(!left.GetEntityByUUID(signID) && !right.GetEntityByUUID(signID), "removed entity still present on viewer");
		Entity local = left.CreateEntity("Local camera");
		uint64_t localID = local.GetUUID();
		Check(!leftView.Apply({ { localID, { 9.f, 9.f, 9.f }, {} } }), "local entity UUID collision accepted");
		Check(left.GetEntityByUUID(boxID) && local.GetComponent<TransformComponent>().Translation.x == 0.f, "local scene changed on a conflict");

		Scene released;
		{
			NetworkScene temporary(released);
			Check(temporary.Apply(a.Poses()), "create transient network entities");
		}
		Check(!released.GetEntityByUUID(boxID), "display entity survived its network layer");
		// 游戏移除一个远端副本后，可能用同一 UUID 从存档恢复本地物体。
		Scene restored;
		{
			NetworkScene remote(restored);
			Check(remote.Apply(a.Poses()), "populate remote scene before restore");
			restored.DestroyEntity(restored.GetEntityByUUID(boxID));
			Entity saved = restored.CreateEntityWithUUID(boxID, "Saved crate");
			saved.GetComponent<TransformComponent>().Translation.y = 7.f;
			Check(!remote.Apply(a.Poses()), "snapshot overwrote a restored local object");
			Check(saved.GetComponent<TransformComponent>().Translation.y == 7.f, "snapshot moved the saved crate");
		}
		Check(restored.GetEntityByUUID(boxID) && !restored.GetEntityByUUID(flatID),
			"disconnect should remove only entities created by the network scene");
		simulation.OnSimulationStop();
		std::printf("PASS physics_host=1 viewers=2 PhysX_y=%.7f Box2D_y=%.7f snapshots=3 remote_removed=1\n", physxY, box2dY);
	}
	catch (const std::exception& e)
	{
		std::fprintf(stderr, "%s\n", e.what());
		return 1;
	}
}
