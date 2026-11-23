#include "rfpch.h"
#include "ActorAnimator.h"
#include "MontageRuntime.h"
#include "Redflag/Scene/Entity.h"
#include "Redflag/Renderer/Model.h"
#include "Redflag/Assets/AssetManager.h"

namespace Redflag
{
	bool ActorAnimator::Play(Entity actor, const std::string& id, bool restart)
	{
		const AnimationState* state = Controller.Find(id);
		if (!state || !actor.HasComponent<ModelRendererComponent>() || !actor.HasComponent<ModelAnimationComponent>()) return false;
		if (state->MontageAsset) {
			const uint64_t before = AddMontagePlayer(actor).LastEvent();
			if (!PlayMontage(actor,state->MontageAsset,"",id,restart)) return false;
			if (MontageSlot.empty()) MontageCursor = before;
			MontageSlot = state->MontageAsset->Slot;
			LastWasMontage = true;
			Timeline.Cancel();
			return true;
		}
		ModelAnimationComponent& player = actor.GetComponent<ModelAnimationComponent>();
		LastWasMontage = false;
		if (!restart && Timeline.State()==id && player.Clip==Clip && player.Playing) return true;

		ModelRendererComponent& renderer = actor.GetComponent<ModelRendererComponent>();
		if (!renderer.ModelAsset && renderer.ModelID) renderer.ModelAsset=AssetManager::Load<Model>(renderer.ModelID);
		const Ref<ModelAnimation> animation = renderer.ModelAsset ? renderer.ModelAsset->GetAnimation() : nullptr;
		if (!animation) return false;
		const auto& clips=animation->GetClips();
		const auto found=std::find_if(clips.begin(),clips.end(),[&](const auto& item){return item.Name==state->Clip;});
		if (found==clips.end() || !Timeline.Play(*state,found->Duration)) return false;

		Clip=state->Clip;
		player.PlayClip(Clip, &Controller, state, true);
		player.Loop=state->Loop;
		player.MatchTravel=false;
		return true;
	}

	bool ActorAnimator::Read(Entity actor, AnimationNotice& notice)
	{
		if (!MontageSlot.empty()) {
			MontageEvent event;
			MontagePlayer* montage = GetMontagePlayer(actor);
			while (montage && montage->Read(MontageCursor,event)) {
				const AnimationState* state = Controller.Find(event.Notice.State);
				if (!state || !state->MontageAsset || state->MontageAsset->Slot != event.Slot) continue;
				notice = event.Notice;
				return true;
			}
		}
		return Timeline.Read(notice);
	}

	bool ActorAnimator::IsPlaying(Entity actor) const
	{
		const MontagePlayer* montage = GetMontagePlayer(actor);
		return LastWasMontage ? montage && montage->IsPlaying(MontageSlot) : Timeline.Playing();
	}

	bool ActorAnimator::IsOpen(Entity actor, const std::string& name) const
	{
		const MontagePlayer* montage = GetMontagePlayer(actor);
		return (!MontageSlot.empty() && montage && montage->IsOpen(MontageSlot,name)) || Timeline.IsOpen(name);
	}
}
