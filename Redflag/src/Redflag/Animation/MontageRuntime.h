#pragma once

#include "MontagePlayer.h"

namespace Redflag
{
	class Entity;

	MontagePlayer* GetMontagePlayer(Entity actor);
	MontagePlayer& AddMontagePlayer(Entity actor);
	bool PlayMontage(Entity actor, const Ref<const AnimationMontage>& asset, const std::string& section,
		const std::string& state, bool restart);
	bool PlayMontageFile(Entity actor, const std::string& file, const std::string& section, bool restart);
}
