#pragma once

#include <cstdint>
#include <string>

namespace Redflag
{
	class Scene;

	enum class ActionResult
	{
		Running, Completed, Cancelled, MissingTarget, OutOfReach, Unavailable
	};

	//========== 生命与可达距离 ==========

	/// <summary>从目标的生命或耐久中扣除伤害。两个实体配置了相同阵营时会阻止这次伤害。</summary>
	/// <returns>目标的数值确实降低时返回 true。</returns>
	bool ApplyDamage(Scene& scene, uint64_t source, uint64_t target, float amount);

	/// <summary>从角色的位置看向目标，检查距离和中间的碰撞体。射线会跳过角色自己的身体。</summary>
	/// <param name="range">两个实体原点之间允许的最远距离，单位为米。</param>
	bool CanReach(Scene& scene, uint64_t actor, uint64_t target, float range);

	//========== 拾取与交付 ==========

	/// <summary>角色够到物体后，将整份物品放进库存，再从场景中移除这个物体。</summary>
	ActionResult CollectItem(Scene& scene, uint64_t actor, uint64_t target, float range);

	/// <summary>双方在交付距离内且库存足够时，将指定数量从交付者转到接收者。</summary>
	ActionResult GiveItem(Scene& scene, uint64_t actor, uint64_t recipient, const std::string& item, int count, float range);

	/* 每个正在交互的角色持有一份。读条时目标仍可能离开、消失或被队友先一步修好。 */
	class Interaction
	{
	public:
		/// <summary>角色够到目标并且目标接受这个动作时开始计时。时间设为零的交互会当场完成。</summary>
		ActionResult Start(Scene& scene, uint64_t actor, uint64_t target, const std::string& action);

		/// <summary>重新检查目标，累计本帧时间。达到设定时长时只应用一次恢复效果。</summary>
		/// <param name="seconds">从上一帧到这一帧经过的秒数。</param>
		ActionResult Update(Scene& scene, float seconds);

		/// <summary>立即结束正在进行的交互。重新开始同一动作时，计时从零开始。</summary>
		void Cancel();

		bool IsRunning() const { return m_Result == ActionResult::Running; }
		float Progress() const { return m_Progress; }

	private:
		uint64_t m_Actor = 0, m_Target = 0;
		std::string m_Action;
		float m_Elapsed = 0.0f, m_Progress = 0.0f;
		ActionResult m_Result = ActionResult::Cancelled;
	};
}
