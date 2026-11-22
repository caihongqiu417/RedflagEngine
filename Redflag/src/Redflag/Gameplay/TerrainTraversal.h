#pragma once

#include <cstdint>
#include <glm/vec3.hpp>

namespace Redflag
{
	class Scene;

	enum class TraversalMode { Ground, Slope, Step, Jump, Climb, Blocked };
	enum class TraversalInput { Walk, Jump, Automatic };

	/* 玩家和队友使用相同的世界坐标与物理碰撞面。动作只在本次游戏的角色实体上保存。 */
	struct TerrainTraversal
	{
		TraversalMode Mode = TraversalMode::Ground;
		glm::vec3 SupportNormal{0,1,0}, Start{0}, Landing{0}, Ledge{0};
		float Clock = 0.f, Duration = 0.f, Clearance = 0.f;
		bool HasSupport = false;

		bool InAir() const { return Mode == TraversalMode::Jump || Mode == TraversalMode::Climb; }
	};

	/// <summary>沿 delta 指定的地面方向移动。主动跳跃和队友自动通行会先检查身体经过的空间及落脚处。</summary>
	/// <param name="delta">本帧的水平位移，单位为米。跨过缺口后脚底应落在另一侧。</param>
	TraversalMode MoveCharacter(Scene& scene, uint64_t actor, glm::vec3 delta, float radius, float seconds,
		TraversalInput input = TraversalInput::Walk);

	void CancelTraversal(Scene& scene, uint64_t actor);
}
