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
		Vector3,
		Entity
	};

	// 返回字段类型对应的字节数，用于在编辑器缓存中按值存储
	size_t ScriptFieldTypeSize(ScriptFieldType type);

	struct ScriptField
	{
		std::string Name;
		ScriptFieldType Type = ScriptFieldType::None;
		MonoClassField* ClassField = nullptr;

		// C# 字段的默认值（即代码里 = xxx 的初始化结果），编辑器面板未命中缓存时回退显示
		std::vector<uint8_t> DefaultValue;
	};

	//MonoClass��װ��
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
		// 编辑器模式下每个实体的脚本字段值缓存：字段名 → 原始字节序列
		// 进入 Play 时会被复制到真正的 ScriptInstance，实现"编辑器里设置默认值"
		using EntityScriptFieldMap = std::unordered_map<std::string, std::vector<uint8_t>>;

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
		static void OnDestroyEntity(UUID entityUUID);

		static Scene* GetSceneContext();

		static Ref<ScriptInstance> GetEntityScriptInstance(UUID entityUUID);
		static std::unordered_map<std::string, Ref<ScriptClass>> GetEntityClasses();

		// 获取实体的字段缓存引用（不存在则返回空 map），编辑器面板读写都通过这里
		static EntityScriptFieldMap& GetScriptFieldCache(UUID entityUUID);

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