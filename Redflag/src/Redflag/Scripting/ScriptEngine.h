#pragma once
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "Redflag/Core/UUID.h"

struct _MonoObject;
struct _MonoMethod;
struct _MonoClass;
struct _MonoClassField;
struct _MonoAssembly;
struct _MonoImage;

using MonoObject = _MonoObject;
using MonoMethod = _MonoMethod;
using MonoClass = _MonoClass;
using MonoClassField = _MonoClassField;
using MonoAssembly = _MonoAssembly;
using MonoImage = _MonoImage;

namespace Redflag {
	class Entity;
	class Scene;

	enum class ScriptFieldType
	{
		None = 0,
		Float,
		Int,
		Bool,
		Vector3
	};

	struct ScriptField
	{
		std::string Name;
		ScriptFieldType Type = ScriptFieldType::None;
		MonoClassField* ClassField = nullptr;
	};

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
		std::string GetFullName() const;
		const std::vector<ScriptField>& GetFields() const { return m_Fields; }
		const ScriptField* GetField(const std::string& name) const;

	private:
		std::string m_ClassNamespace;
		std::string m_ClassName;

		MonoClass* m_MonoClass = nullptr;
		std::vector<ScriptField> m_Fields;

		friend class ScriptEngine;
	};


	class ScriptInstance
	{
	public:
		ScriptInstance(Ref<ScriptClass> ScriptClass, Entity entity);

		bool GetFieldValue(const ScriptField& field, void* buffer) const;
		bool SetFieldValue(const ScriptField& field, const void* value);

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
		static bool IsRuntimeActive();

		static bool UnloadAssemblies();
		static bool LoadAssemblies();
		static bool ReloadAssemblies();
		static const std::string& GetAssemblyStatusMessage();

		static bool EntityClassExists(const std::string& fullClassName);

		static void OnCreateEntity(Entity entity);
		static void OnUpdateEntity(Entity entity,float ts);

		static Scene* GetSceneContext();

		static Ref<ScriptInstance> GetEntityScriptInstance(UUID entityUUID);
		static std::unordered_map<std::string, Ref<ScriptClass>> GetEntityClasses();

	private:
		static void InitMono();
		static void ShutdownMono();
		static MonoAssembly* LoadAssembly(const std::filesystem::path& filepath);

		static MonoObject* InstantiateClass(MonoClass* monoClass);
		static void LoadAssemblyClasses(MonoAssembly* assembly);
		static void LoadClassFields(const Ref<ScriptClass>& scriptClass);

		friend class ScriptClass;
	};
}