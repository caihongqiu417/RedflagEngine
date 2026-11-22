#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace Redflag
{
	struct HudComponent
	{
		std::string Layout;
	};

	struct HealthComponent
	{
		float Current = 100.0f, Maximum = 100.0f;
		std::string Team;
	};

	struct InventoryComponent
	{
		std::map<std::string, int> Items;
	};

	struct PickupComponent
	{
		std::string Item;
		int Count = 1;
	};

	/* 角色靠近目标并完成指定动作后，目标恢复生命或耐久。 */
	struct InteractionComponent
	{
		std::string Action = "repair";
		float Range = 2.0f, Seconds = 2.0f, Restore = 50.0f;
		bool OnlyWhenDown = false;
	};
}
