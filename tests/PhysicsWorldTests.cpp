#include "Redflag/Physics/PhysicsWorld.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace Redflag;

static void Check(bool value, const char* message)
{
	if (!value)
		throw std::runtime_error(message);
}

int main()
{
	PhysicsWorld room;
	constexpr uint64_t floor = 11, crate = 0xf123000000000013ull, platform = 19;
	const glm::quat upright(1, 0, 0, 0);
	room.AddBox({ floor, BodyType3D::Static, { 0, -0.5f, 0 }, upright, { 3, 0.5f, 3 } });
	room.AddBox({ crate, BodyType3D::Dynamic, { 0, 3, 0 }, upright, glm::vec3(0.5f) });

	glm::vec3 position;
	glm::quat rotation;
	for (int i = 0; i < 60; ++i)
		room.Step(1.0f / 120.0f);
	Check(room.GetPose(crate, position, rotation) && std::abs(position.y - 1.732875f) < 0.01f, "free fall");
	for (int i = 0; i < 90; ++i)
		room.Step(1.0f / 60.0f);
	Check(room.GetPose(crate, position, rotation) && std::abs(position.y - 0.5f) < 0.01f, "floor contact");
	float restHeight = position.y;

	auto hit = room.Raycast({ 0, 3, 0 }, { 0, -2, 0 }, 3);
	Check(hit && hit->Entity == crate && std::abs(hit->Distance - 2) < .01f && hit->Normal.y > .99f, "nearest hit in metres");
	Check(!room.Raycast({ 0, 3, 0 }, { 0, -1, 0 }, 1), "out of reach");
	auto nearFloor = room.OverlapSphere({ 0, 0, 0 }, .3f);
	Check(nearFloor.size() == 2 && std::find(nearFloor.begin(), nearFloor.end(), crate) != nearFloor.end(), "both touching bodies");

	{
		PhysicsWorld otherRoom;
		otherRoom.AddBox({ platform, BodyType3D::Kinematic, { 0, 2, 0 }, upright, glm::vec3(.25f) });
		otherRoom.SetKinematicTarget(platform, { 3, 2, 0 }, upright);
		otherRoom.Step(1.0f / 60.0f);
		Check(otherRoom.GetPose(platform, position, rotation) && position.x == 3, "moving platform");
		Check(!otherRoom.GetPose(crate, position, rotation), "world isolation");
	}
	room.Remove(crate);
	hit = room.Raycast({ 0, 3, 0 }, { 0, -1, 0 }, 4);
	Check(hit && hit->Entity == floor, "removed object still blocks ray");
	std::cout << "PASS gravity, contact, ray, overlap, moving platform, two scenes and removal; resting y = " << restHeight << " m\n";
}
