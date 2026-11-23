#include "rfpch.h"
#include "Redflag/Animation/MontagePlayer.h"
#include "Redflag/Scene/Entity.h"

using namespace Redflag;
using LocalPose = std::vector<ModelAnimation::NodePose>;

static void Check(bool pass, const std::string& name)
{
	if (!pass) throw std::runtime_error(name);
	std::cout << "PASS " << name << '\n';
}

static size_t Clip(const ModelAnimation& model, const std::string& name)
{
	const auto& clips = model.GetClips();
	const auto found = std::find_if(clips.begin(),clips.end(),[&](const auto& clip) { return clip.Name == name; });
	if (found == clips.end()) throw std::runtime_error(name);
	return found - clips.begin();
}

static float Difference(const ModelAnimation::NodePose& a, const ModelAnimation::NodePose& b)
{
	return glm::distance(a.Position,b.Position) + glm::distance(a.Scale,b.Scale) +
		std::min(glm::length(a.Rotation-b.Rotation),glm::length(a.Rotation+b.Rotation));
}

static void CheckMaskedPose(const Ref<ModelAnimation>& model)
{
	std::string error;
	auto fire = CreateRef<AnimationMontage>();
	Check(fire->Load("assets/ai/montages/fire.montage.yaml",error),error + " load real upper-body montage");
	MontagePlayer player;
	Check(player.Play(fire,model,"","fire",false,error),error + " play upper body over walk");
	player.Advance(.1f);
	std::vector<size_t> upper;
	Check(model->SubtreeMask(fire->Bones,upper) && !upper.empty(),"resolve the actual spine descendants");
	LocalPose walk, shot;
	model->SamplePose(Clip(*model,fire->Clip),.1f,shot);
	const size_t gait = Clip(*model,"walking_nogun-cycle");
	for (float time : {.09f,.37f}) {
		model->SamplePose(gait,time,walk);
		LocalPose output = walk;
		player.Apply(*model,output);
		float changedUpper = 0;
		for (size_t i = 0; i < output.size(); ++i) {
			const bool affected = std::find(upper.begin(),upper.end(),i) != upper.end();
			if (affected) changedUpper += Difference(output[i],walk[i]);
			if (Difference(output[i],affected ? shot[i] : walk[i]) > 1e-5f) throw std::runtime_error("incorrect pose layer");
		}
		Check(changedUpper > .01f,"sampled walk is preserved below the spine and shooting pose is visible above it");
	}
	Check(player.Stop(fire->Slot),"start blend-out from current weight");
	player.Advance(.07f);
	LocalPose middle = walk;
	player.Apply(*model,middle);
	const size_t hand = *std::max_element(upper.begin(),upper.end(),[&](size_t a,size_t b) {
		return Difference(shot[a],walk[a]) < Difference(shot[b],walk[b]);
	});
	Check(Difference(middle[hand],walk[hand]) > .0001f && Difference(middle[hand],shot[hand]) > .0001f,
		"blend-out interpolates the arm before returning to locomotion");
	player.Advance(.2f);
	Check(!player.HasPose(),"completed fade releases the slot layer");
}

static void CheckSections(const Ref<ModelAnimation>& model)
{
	auto asset = CreateRef<AnimationMontage>();
	asset->Name = "inspect";
	asset->Clip = "Station-Give";
	asset->Slot = "Gesture";
	asset->Sections = {{"reach",0,.4f,"hold"},{"hold",.4f,.8f,""},{"withdraw",1.f,1.2f,""}};
	asset->Events = {{"start",0,{}},{"window",.2f,.6f},{"touch",.4f,{}},{"finish",1.1f,{}}};
	std::string error;
	MontagePlayer player;
	Check(player.Play(asset,model,"","inspect",false,error),"play first section");
	player.Advance(.45f);
	Check(player.IsOpen("Gesture","window"),"window stays open across contiguous sections");
	Check(player.Jump("Gesture","withdraw"),"jump to non-contiguous section");
	player.Advance(.2f);
	uint64_t cursor=0;
	MontageEvent event;
	std::vector<std::string> actual;
	while (player.Read(cursor,event)) actual.push_back(event.Notice.Name + (event.Notice.Interrupted ? "!" : ""));
	Check(actual == std::vector<std::string>{"start","window","touch","window!","finish"},
		"section jump closes old window and skips its later marks");
	Check(!player.IsPlaying("Gesture"),"section end without Next finishes");

	Check(player.Play(asset,model,"hold","inspect",true,error) && player.IsOpen("Gesture","window"),"starting inside an open window emits Begin");
	Check(player.Stop("Gesture") && !player.IsOpen("Gesture","window"),"interruption closes the active window");
	uint64_t otherReader=0;
	Check(player.Read(otherReader,event) && event.Notice.Name=="start","script and debugger can read notices independently");
}

static void CheckWarp(const Ref<ModelAnimation>& model)
{
	auto asset = CreateRef<AnimationMontage>();
	asset->Name = "step";
	asset->Clip = "walking_nogun-cycle";
	asset->Slot = "Body";
	asset->Sections = {{"step",0,.6f,""}};
	asset->WarpWindows = {{"stand","root",.1f,.5f}};
	MontagePlayer player;
	std::string error;
	Check(player.Play(asset,model,"","step",false,error),"start motion with named warp window");
	player.SetTarget("stand",{{2,0,1},0});
	glm::vec3 world(0);
	size_t steps = 0;
	float authored = 0.f;
	for (int i=0;i<12;++i) player.Advance(.05f,[&](const MontageTarget& target,const glm::vec3& delta,
		const glm::vec3& remaining,float t,float) {
		world += delta + (target.Position - world - remaining) * t;
		authored += glm::length(delta);
		++steps;
	});
	Check(steps >= 8 && authored > .01f && glm::distance(world,{2,0,1}) < .0001f,"sampled root motion converges over multiple steps");

	// 两条并排的路线用同一份原片和目标距离，第二条路线中间放一堵两米高的实体墙。
	Scene scene;
	auto box = [&](glm::vec3 center, glm::vec3 half) {
		Entity object = scene.CreateEntity("obstacle");
		object.GetComponent<TransformComponent>().Translation = center;
		object.AddComponent<RigidBody3DComponent>();
		object.AddComponent<BoxCollider3DComponent>().HalfSize = half;
	};
	box({0,-.2f,0},{4,.2f,4});
	box({1.f,1.f,2.f},{.1f,1.f,.85f});
	scene.OnSimulationStart();
	for (float lane : {0.f,2.f}) {
		Entity actor = scene.CreateEntity("walker");
		actor.GetComponent<TransformComponent>().Translation = {0,.01f,lane};
		actor.AddComponent<RigidBody3DComponent>();
		CapsuleCollider3DComponent& body = actor.AddComponent<CapsuleCollider3DComponent>();
		body.Center.y = body.Height * .5f;
		actor.AddComponent<ModelRendererComponent>();
		actor.AddComponent<ModelAnimationComponent>();
		MontagePlayer& runtime = actor.AddComponent<MontagePlayer>();
		Check(runtime.Play(asset,model,"","step",false,error),"starting real scene montage");
		runtime.SetTarget("stand",{{2,0,lane},.4f});

		// 动画更新负责实体移动。每帧都通过 PhysX 查询墙和地面。
		for (int frame=0; frame<40; ++frame) {
			scene.OnUpdatePhysics(1.f/60);
			scene.OnUpdateAnimations(1.f/60);
		}
		const TransformComponent& actual = actor.GetComponent<TransformComponent>();
		std::cout << "warp lane " << lane << " -> (" << actual.Translation.x << "," << actual.Translation.z << "), yaw " << actual.Rotation.y << '\n';
		Check(lane == 0 ? std::abs(actual.Translation.x-2.f)<.001f : actual.Translation.x>0 && actual.Translation.x<.9f,
			lane == 0 ? "unblocked character reaches the target in world space" : "solid wall stops the character before the warp target");
		Check(std::abs(actual.Rotation.y-.4f)<.001f,"yaw follows the named world target");
		scene.DestroyEntity(actor);
	}
	scene.OnSimulationStop();
}

int main()
{
	try {
		Log::Init();
		const Ref<ModelAnimation> model = ModelAnimation::Load("assets/game/station/crew/Operator.glb");
		Check(model != nullptr,"load shipping rig and clips");
		CheckMaskedPose(model);
		CheckSections(model);
		CheckWarp(model);
		return 0;
	} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
