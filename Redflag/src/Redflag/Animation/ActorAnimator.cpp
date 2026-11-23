#include "rfpch.h"
#include "ActorAnimator.h"
#include "Redflag/Scene/Entity.h"
#include "Redflag/Renderer/Model.h"
#include "Redflag/Assets/AssetManager.h"

namespace Redflag
{
	bool ActorAnimator::Play(Entity actor, const std::string& id, bool restart)
	{
		const AnimationState* state = Controller.Find(id);
		if (!state || !actor.HasComponent<ModelRendererComponent>() || !actor.HasComponent<ModelAnimationComponent>()) return false;
		ModelAnimationComponent& player = actor.GetComponent<ModelAnimationComponent>();
		if (!restart && Timeline.State()==id && player.Clip==Clip && player.Playing) return true;

		ModelRendererComponent& renderer = actor.GetComponent<ModelRendererComponent>();
		if (!renderer.ModelAsset && renderer.ModelID) renderer.ModelAsset=AssetManager::Load<Model>(renderer.ModelID);
		const Ref<ModelAnimation> animation = renderer.ModelAsset ? renderer.ModelAsset->GetAnimation() : nullptr;
		if (!animation) return false;
		const auto& clips=animation->GetClips();
		const auto found=std::find_if(clips.begin(),clips.end(),[&](const auto& item){return item.Name==state->Clip;});
		if (found==clips.end() || !Timeline.Play(*state,found->Duration)) return false;

		Clip=state->Clip;
		player.Clip=Clip;
		player.Time=0.f;
		player.Playing=true;
		player.Loop=state->Loop;
		player.MatchTravel=false;
		player.BlendSeconds=state->Blend;
		return true;
	}
}
