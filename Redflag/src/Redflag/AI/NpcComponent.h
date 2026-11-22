#pragma once
#include <cstdint>
#include <string>

namespace Redflag
{
	/* 多个角色可以引用同一份定义。Leader 指向本局里这个角色自己的队友。 */
	struct NpcComponent
	{
		std::string Definition, VoiceOverride;
		uint64_t Leader = 0;
	};
}
