#include "rfpch.h"
#include "NpcScriptAction.h"
#include <mono/metadata/appdomain.h>
#include <mono/metadata/object.h>
#include <map>

namespace Redflag
{
	static std::map<std::string, MonoClass*> s_ActionTypes;
	static MonoMethod* s_Tick = nullptr;
	static MonoMethod* s_Cancel = nullptr;

	//========== 项目脚本与行为树菜单 ==========

	void NpcScriptAction::Register(const std::string& name, MonoClass* type)
	{
		s_ActionTypes[name] = type;
	}

	void NpcScriptAction::ClearTypes()
	{
		s_ActionTypes.clear();
		s_Tick = s_Cancel = nullptr;
	}

	void NpcScriptAction::SetBaseType(MonoClass* type)
	{
		s_Tick = mono_class_get_method_from_name(type, "Tick", 3);
		s_Cancel = mono_class_get_method_from_name(type, "Cancel", 2);
	}

	std::vector<std::string> NpcScriptAction::Names()
	{
		std::vector<std::string> names;
		for (const auto& entry : s_ActionTypes) names.push_back(entry.first);
		return names;
	}

	bool NpcScriptAction::Exists(const std::string& name)
	{
		return s_ActionTypes.find(name) != s_ActionTypes.end();
	}

	//========== 一次动作的建立、推进与中止 ==========

	static MonoObject* Invoke(MonoMethod* method, MonoObject* owner, void** args, bool& failed)
	{
		MonoObject* exception = nullptr;
		MonoObject* result = mono_runtime_invoke(method, owner, args, &exception);
		failed = exception != nullptr;
		if (failed)
			RF_CORE_ERROR("NPC script {} failed in {}", mono_method_get_name(method),
				mono_class_get_name(mono_object_get_class(exception)));
		return result;
	}

	Ref<NpcScriptAction> NpcScriptAction::Create(const std::string& name)
	{
		const auto entry = s_ActionTypes.find(name);
		if (entry == s_ActionTypes.end()) return nullptr;

		MonoClass* type = entry->second;
		MonoObject* instance = mono_object_new(mono_domain_get(), type);
		MonoMethod* constructor = mono_class_get_method_from_name(type, ".ctor", 0);
		bool failed;
		Invoke(constructor, instance, nullptr, failed);
		if (failed) return nullptr;

		// 从基类的虚方法定位真正的 override，中间还有一层项目自定义基类时也有效。
		MonoMethod* tick = mono_object_get_virtual_method(instance, s_Tick);
		MonoMethod* cancel = mono_object_get_virtual_method(instance, s_Cancel);
		return Ref<NpcScriptAction>(new NpcScriptAction(mono_gchandle_new(instance, false), tick, cancel));
	}

	NpcScriptAction::NpcScriptAction(uint32_t handle, MonoMethod* tick, MonoMethod* cancel)
		: m_Handle(handle), m_Tick(tick), m_Cancel(cancel) {}

	NpcScriptAction::~NpcScriptAction()
	{
		mono_gchandle_free(m_Handle);
	}

	ActionResult NpcScriptAction::Tick(uint64_t actor, uint64_t target, float seconds)
	{
		void* args[] = { &actor, &target, &seconds };
		bool failed;
		MonoObject* result = Invoke(m_Tick, mono_gchandle_get_target(m_Handle), args, failed);
		return failed ? ActionResult::Unavailable : *static_cast<ActionResult*>(mono_object_unbox(result));
	}

	void NpcScriptAction::Cancel(uint64_t actor, uint64_t target)
	{
		void* args[] = { &actor, &target };
		bool failed;
		Invoke(m_Cancel, mono_gchandle_get_target(m_Handle), args, failed);
	}
}
