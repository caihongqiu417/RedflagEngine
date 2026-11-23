#include "rfpch.h"
#include "MontageRuntime.h"
#include "Redflag/Assets/AssetManager.h"
#include "Redflag/Scene/Entity.h"

namespace Redflag
{
	MontagePlayer* GetMontagePlayer(Entity actor)
	{
		return actor && actor.HasComponent<MontagePlayer>() ? &actor.GetComponent<MontagePlayer>() : nullptr;
	}

	MontagePlayer& AddMontagePlayer(Entity actor)
	{
		if (MontagePlayer* existing = GetMontagePlayer(actor)) return *existing;
		return actor.AddComponent<MontagePlayer>();
	}

	bool PlayMontage(Entity actor, const Ref<const AnimationMontage>& asset, const std::string& section,
		const std::string& state, bool restart)
	{
		if (!actor || !actor.HasComponent<ModelAnimationComponent>() || !actor.HasComponent<ModelRendererComponent>()) return false;
		ModelRendererComponent& renderer = actor.GetComponent<ModelRendererComponent>();
		if (!renderer.ModelAsset && renderer.ModelID) renderer.ModelAsset = AssetManager::Load<Model>(renderer.ModelID);
		if (!renderer.ModelAsset) return false;

		std::string error;
		if (AddMontagePlayer(actor).Play(asset,renderer.ModelAsset->GetAnimation(),section,state,restart,error)) return true;
		RF_CORE_WARN("Montage: {0}", error);
		return false;
	}

	bool PlayMontageFile(Entity actor, const std::string& file, const std::string& section, bool restart)
	{
		if (!actor) return false;
		MontagePlayer& player = AddMontagePlayer(actor);
		Ref<const AnimationMontage>& saved = player.Files[file];
		if (!saved) {
			auto asset = CreateRef<AnimationMontage>();
			std::string error;
			if (!asset->Load(std::filesystem::u8path(file),error)) {
				RF_CORE_WARN("Montage: {0}",error);
				player.Files.erase(file);
				return false;
			}
			saved = asset;
		}
		return PlayMontage(actor,saved,section,saved->Name,restart);
	}
}
