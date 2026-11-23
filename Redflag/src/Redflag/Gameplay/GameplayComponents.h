#pragma once

#include <cstdint>
#include <map>
#include <glm/vec3.hpp>
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

	/* Point 是模型或骨头局部坐标，单位为米。Stand 指定面对设备时双脚站在哪一侧。 */
	struct ContactSocket
	{
		std::string Bone;
		glm::vec3 Point{0}, Stand{0};
	};

	struct ContactSocketsComponent
	{
		std::map<std::string, ContactSocket> Points;
		std::string ItemModel;
        // 握持点与目标可以相差多少米；交接时两人的站位相隔多少米。
        float GripTolerance = .04f, GiveDistance = .87f;
	};

	struct BehaviorTreeComponent
	{
		std::string File;
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
