#pragma once
#include <string>
#include <glm/vec3.hpp>
#include <chrono>
#include <map>

namespace Redflag
{
	/* 关卡脚本写入当前任务、队伍状态和操作提示，游戏视图显示这些文字。 */
	struct GameHud
	{
		std::map<std::string, std::string> Text;
		std::map<std::string, float> Meters;
		bool Finished = false;
	};

	struct Nameplate
	{
		glm::vec3 Color;
		float Height; // 名字距角色脚底的高度，单位是米。
	};

	struct PulseTrace
	{
		glm::vec3 From, To;
		std::chrono::steady_clock::time_point Until;
	};
}
