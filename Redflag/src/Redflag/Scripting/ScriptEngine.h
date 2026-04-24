#pragma once
#include <filesystem>
#include <string>

struct _MonoObject;
struct _MonoMethod;
struct _MonoClass;

using MonoObject = _MonoObject;
using MonoMethod = _MonoMethod;
using MonoClass = _MonoClass;

namespace Redflag {


	class ScriptEngine
	{
	public:
		static void Init();

		static void Shutdown();

		static void LoadAssembly(const std::filesystem::path& filepath);

	private:
		static void InitMono();
		static void ShutdownMono();

		static MonoObject* InstantiateClass(MonoClass* monoClass);

		friend class ScriptClass;
	};

	class ScriptClass
	{
	public:
		ScriptClass() = default;

		ScriptClass(const std::string& classNamespace, const std::string& className);

		MonoObject* Instantiate();

		MonoMethod* GetMethod(const std::string& name, int parameterCount);
		MonoObject* InvokeMethod(MonoObject* instance,MonoMethod* method, void** params);

	private:
		std::string m_ClassNamespace;
		std::string m_ClassName;

		MonoClass* m_MonoClass = nullptr;
	};
}