#pragma once

#include "Redflag/Core/Core.h"
#include "Redflag/Gameplay/WorldActions.h"
#include "ScriptEngine.h"

namespace Redflag
{
	/* 脚本类型随程序集加载。角色真正开始动作时才创建实例，结束后释放对托管对象的引用。 */
	class NpcScriptAction
	{
	public:
		static void Register(const std::string& name, MonoClass* type);
		static void SetBaseType(MonoClass* type);
		static void ClearTypes();
		static std::vector<std::string> Names();
		static bool Exists(const std::string& name);
		static Ref<NpcScriptAction> Create(const std::string& name);

		~NpcScriptAction();
		NpcScriptAction(const NpcScriptAction&) = delete;
		NpcScriptAction& operator=(const NpcScriptAction&) = delete;

		ActionResult Tick(uint64_t actor, uint64_t target, float seconds);
		void Cancel(uint64_t actor, uint64_t target);

	private:
		NpcScriptAction(uint32_t handle, MonoMethod* tick, MonoMethod* cancel);

		uint32_t m_Handle;
		MonoMethod* m_Tick;
		MonoMethod* m_Cancel;
	};
}
