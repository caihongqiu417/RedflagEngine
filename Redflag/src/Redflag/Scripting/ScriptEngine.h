#pragma once
#include <filesystem>
#include <string>
#include <unordered_map>

struct _MonoObject;
struct _MonoMethod;
struct _MonoClass;
struct _MonoAssembly;
struct _MonoImage;

using MonoObject = _MonoObject;
using MonoMethod = _MonoMethod;
using MonoClass = _MonoClass;
using MonoAssembly = _MonoAssembly;
using MonoImage = _MonoImage;

namespace Redflag {
	class Entity;
	class Scene;

	//MonoClass°ü×°Àà
	class ScriptClass
	{
	public:
		ScriptClass() = default;

		ScriptClass(MonoImage* image, const std::string& classNamespace, const std::string& className);

		MonoObject* Instantiate();

		MonoMethod* GetMethod(const std::string& name, int parameterCount);
		MonoObject* InvokeMethod(MonoObject* instance, MonoMethod* method, void** params);
		bool IsValid() const { return m_MonoClass != nullptr; }

	private:
		std::string m_ClassNamespace;
		std::string m_ClassName;

		MonoClass* m_MonoClass = nullptr;
	};


	class ScriptInstance
	{
	public:
		ScriptInstance(Ref<ScriptClass> ScriptClass, Entity entity);

		void InvokeOnCreate();
		void InvokeOnUpdate(float ts);

	private:
		Ref<ScriptClass> m_ScriptClass;

		MonoObject* m_Instance = nullptr;
		MonoMethod* m_Constructor = nullptr;
		MonoMethod* m_OnCreateMethod = nullptr;
		MonoMethod* m_OnUpdateMethod = nullptr;
	};

	class ScriptEngine
	{
	public:
		static void Init();

		static void Shutdown();

		static void OnRuntimeStart(Scene* scene);
		static void OnRuntimeStop();

		static bool EntityClassExists(const std::string& fullClassName);

		static void OnCreateEntity(Entity entity);
		static void OnUpdateEntity(Entity entity,float ts);

		static Scene* GetSceneContext();

		static std::unordered_map<std::string, Ref<ScriptClass>> GetEntityClasses();

	private:
		static void InitMono();
		static void ShutdownMono();
		static MonoAssembly* LoadAssembly(const std::filesystem::path& filepath);

		static MonoObject* InstantiateClass(MonoClass* monoClass);
		static void LoadAssemblyClasses(MonoAssembly* assembly);

		friend class ScriptClass;
	};
}