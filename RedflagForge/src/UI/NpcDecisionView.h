#pragma once

#include <cstdint>

namespace Redflag
{
	struct NpcDefinition;
	struct NpcState;

	/* Forge 调试面板。请求编号为 0 时跟随最新一条，选中历史请求后保持当前选择。 */
	void DrawNpcRequests(const NpcState* state, uint64_t& request);
	void DrawNpcActions(const NpcDefinition& role, const NpcState* state, uint64_t request);
}
