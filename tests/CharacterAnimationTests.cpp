#include "Redflag/Animation/CharacterController.h"
#include "Redflag/Animation/Ragdoll.h"
#include "Redflag/Animation/SkinnedMesh.h"
#include "Redflag/Core/Log.h"
#include "Platform/Vulkan/VulkanContext.h"
#include "Platform/Vulkan/VulkanDraw.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace Redflag;

namespace
{
	void Check(bool passed, const char* message)
	{
		if (!passed) throw std::runtime_error(message);
	}

	void CheckClips()
	{
		std::filesystem::path folder("assets/animations/GAMES105");
		AnimationClip idle = AnimationClip::LoadBVH(folder / "Idle.bvh");
		AnimationClip walk = AnimationClip::LoadBVH(folder / "Walk.bvh");
		Check(walk.Frames.size() == 100 && walk.Rig.Joints.size() == 20 && idle.Frames.size() == 200, "BVH channel count");
		Pose local = walk.Sample(walk.FrameTime * 3.5f);
		Check(glm::length(local[0].Position - glm::mix(walk.Frames[3][0].Position, walk.Frames[4][0].Position, .5f)) < 1e-5f, "Half-frame translation");
		Check(glm::length(walk.Sample(-walk.FrameTime)[0].Position - walk.Frames.back()[0].Position) < 1e-5f, "Cycle backwards");
		CharacterController actor(idle, walk);
		for (int i = 0; i < 120; ++i)
			local = actor.Update(1.0f / 60, {.6f,.8f});
		Check(glm::length(actor.GetPosition() - glm::vec2(1.2f,1.6f)) < .0001f, "Walk direction and distance");
		for (int i = 0; i < 60; ++i)
			local = actor.Update(1.0f / 60, {0,0});
		Check(glm::length(glm::vec2(local[0].Position.x,local[0].Position.z) - actor.GetPosition()) < .0001f, "Root slid after stop");

		const Skeleton& rig = walk.Rig;
		Pose model = rig.ToModelSpace(local);
		int wrist = rig.Find("lWrist"), elbow = rig.Joints[wrist].Parent, shoulder = rig.Joints[elbow].Parent;
		glm::vec3 target = model[shoulder].Position + glm::vec3(.12f,.20f,.13f);
		glm::vec3 pole = model[shoulder].Position + glm::vec3(.4f,0,0);
		float before = glm::distance(model[wrist].Position, target);
		rig.SolveTwoBone(local, wrist, target, pole);
		model = rig.ToModelSpace(local);
		float after = glm::distance(model[wrist].Position, target);
		Check(after < .0001f && before > .01f, "Reachable two-bone IK");
		float length = glm::length(local[elbow].Position) + glm::length(local[wrist].Position);
		rig.SolveTwoBone(local, wrist, target + glm::vec3(0,3,0), pole);
		model = rig.ToModelSpace(local);
		Check(std::abs(glm::distance(model[wrist].Position,model[shoulder].Position) - length) < .0001f, "Unreachable target stretched bones");
		std::cout << "BVH: 20 joints, walk 100 frames, idle 200 frames; travel: " << glm::length(actor.GetPosition())
			<< " m; wrist miss before/after IK: " << before << " / " << after << " m\n";
	}

	void CheckSkin()
	{
		Skeleton rig{{{"Root",-1,{0,0,0}}, {"Hinge",0,{0,0,0}}}};
		std::vector<MeshVertex> vertices;
		for (glm::vec3 p : {glm::vec3(-.30f,-.30f,0), glm::vec3(.30f,-.30f,0), glm::vec3(0,.30f,0)})
			vertices.push_back({p, {0,0,1}, {}, {1,0,0}});
		std::vector<SkinWeights> weights(3, {{0,1},{.25f,.75f}});
		SkinnedMesh mesh(rig, vertices, weights, {0,1,2});
		Pose rest = rig.ToModelSpace(rig.RestPose());
		Pose moved = rest;
		moved[1].Position.x += .8f;
		std::vector<MeshVertex> active = mesh.Deform(moved);
		Check(glm::distance(active[2].Position,vertices[2].Position + glm::vec3(.6f,0,0)) < .0001f, "Weighted vertex");
		moved[1].Rotation = glm::angleAxis(glm::half_pi<float>(),glm::vec3(0,1,0));
		std::vector<MeshVertex> turned = mesh.Deform(moved);
		Check(glm::distance(turned[0].Normal, glm::normalize(glm::vec3(.75f,0,.25f))) < .0001f, "Weighted surface normal");

		Check(glfwInit() != 0, "GLFW init");
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
		std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window(glfwCreateWindow(96,64,"Skin",nullptr,nullptr),glfwDestroyWindow);
		Check(window != nullptr,"GLFW window");
		VulkanContext context(window.get());
		context.Init();
		VulkanDraw renderer(context);
		renderer.Resize(96,64);
		const uint32_t white = 0xffffffff;
		Ref<SampledTexture> texture = renderer.CreateTexture(1,1,&white);
		for (int f = 0; f < 2; ++f)
		{
			Ref<MeshGeometry> geometry = renderer.CreateMesh(f ? active : mesh.Deform(rest), mesh.GetIndices());
			Check(context.BeginFrame(), "Vulkan frame");
			renderer.BeginScene();
			renderer.DrawIndexed(geometry, texture, glm::mat4(1), glm::mat4(1), glm::vec4(1), 39);
			renderer.EndScene();
			context.SwapBuffers();
			int left = renderer.ReadPixel(48,32).EntityID, right = renderer.ReadPixel(77,32).EntityID;
			std::cout << "Vulkan pose " << f << ", ID at (48,32)/(77,32): " << left << "/" << right << "\n";
			Check(f ? left == -1 && right == 39 : left == 39 && right == -1, "Vulkan skinned triangle not at expected pixel");
		}
	}

	void CheckPhysics()
	{
		PhysicsWorld world;
		world.AddBox({75, BodyType3D::Static, {0,-.25f,0}, {1,0,0,0}, {10,.25f,10}});
		const Ragdoll::Entities entities{101,102,103,104,105,106,107};
		auto position = [&](Ragdoll::Limb limb) {
			glm::vec3 center;
			glm::quat rotation;
			Check(world.GetPose(entities[limb],center,rotation), "Missing limb");
			return center;
		};
		{
			Ragdoll actor(world, entities, {0,0,0});
			Check(world.Raycast({0,2,0}, {0,-1,0}, 5)->Entity == entities[Ragdoll::Pelvis], "Pelvis picking");
			constexpr float dt = 1.0f / 60;
			float minY = 10, leftLift = 0, rightLift = 0, minUp = 1;
			int leftSteps = 0, rightSteps = 0, lastSide = 0;
			int leftOnGround = 0, rightOnGround = 0;
			for (int f = 0; f < 360; ++f)
			{
				actor.DriveWalk(dt, .8f);
				world.Step(dt);
				if (f < 30) continue;
				glm::vec3 center;
				glm::quat rotation;
				world.GetPose(entities[Ragdoll::Pelvis],center,rotation);
				minY = std::min(minY,center.y);
				minUp = std::min(minUp,(rotation * glm::vec3(0,1,0)).y);
				float left = position(Ragdoll::LeftFoot).y, right = position(Ragdoll::RightFoot).y;
				auto soleHeight = [&](Ragdoll::Limb foot) {
					glm::vec3 p; glm::quat q;
					world.GetPose(entities[foot],p,q);
					glm::mat3 axes = glm::mat3_cast(q);
					return p.y - glm::dot(glm::abs(glm::vec3(axes[0].y,axes[1].y,axes[2].y)),Ragdoll::HalfSize(foot));
				};
				leftOnGround += soleHeight(Ragdoll::LeftFoot) < .005f;
				rightOnGround += soleHeight(Ragdoll::RightFoot) < .005f;
				leftLift = std::max(leftLift,left-right);
				rightLift = std::max(rightLift,right-left);
				int side = left-right > .025f ? 1 : right-left > .025f ? -1 : 0;
				if (side && lastSide != side)
				{
					(side > 0 ? leftSteps : rightSteps)++;
					lastSide = side;
				}
			}
			glm::vec3 end = position(Ragdoll::Pelvis);
			std::cout << "PhysX walk 6 s: z=" << end.z << " m; min pelvis height=" << minY <<
				" m, min up.y=" << minUp << "; foot height differences=" << leftLift << "/" << rightLift << " m; swings=" << leftSteps << "/" << rightSteps << "\n";
			Check(end.z > .8f && minY > .55f && minUp > .75f && leftSteps >= 2 && rightSteps >= 2, "Physical gait / balance / alternate feet");
			std::cout << "Foot within 5 mm of plane, out of 330 frames: " << leftOnGround << "/" << rightOnGround << "\n";
			Check(leftOnGround > 0 && rightOnGround > 0 && leftOnGround < 330 && rightOnGround < 330, "Each foot must touch and leave ground");
			for (int i = 0; i < 120; ++i)
			{
				actor.DriveWalk(dt,0.0f);
				world.Step(dt);
			}
			float stopDistance = std::abs(position(Ragdoll::Pelvis).z-end.z);
			std::cout << "After 2 s with zero input, pelvis drift: " << stopDistance << " m\n";
			Check(stopDistance < .5f && position(Ragdoll::Pelvis).y > .7f,"Stopped gait");
		}
		Check(world.Raycast({0,2,0}, {0,-1,0}, 5)->Entity == 75, "Removed ragdoll still hit");
		{
			Ragdoll falling(world,entities,{0,1,0});
			falling.Relax();
			float before = position(Ragdoll::Pelvis).y;
			for (int f = 0; f < 180; ++f) world.Step(1.0f/60);
			float after = position(Ragdoll::Pelvis).y;
			std::cout << "Released pelvis y: " << before << " -> " << after << " m\n";
			Check(after < before-1.1f && after > 0.0f, "Passive ragdoll drop");
		}
	}
}

int RunCharacterDemo(bool sequence);

int main(int argc, char** argv)
{
	try
	{
		Log::Init();
		if (argc > 1)
		{
			std::string mode = argv[1];
			Check(mode == "--demo" || mode == "--sequence","Expected --demo or --sequence");
			int result = RunCharacterDemo(mode == "--sequence");
			glfwTerminate();
			return result;
		}
		CheckClips();
		CheckPhysics();
		CheckSkin();
		glfwTerminate();
		return 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "\n";
		return 1;
	}
}
