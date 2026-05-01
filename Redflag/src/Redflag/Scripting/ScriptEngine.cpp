#include "rfpch.h"
#include "ScriptEngine.h"

#include "mono/jit/jit.h"
#include"mono/metadata/assembly.h"
#include "mono/metadata/attrdefs.h"
#include "mono/metadata/blob.h"
#include "mono/metadata/class.h"
#include "mono/metadata/metadata.h"
#include"glm/glm.hpp"
#include"mono/metadata/object.h"
#include"ScriptGlue.h"

#include"Scene/Scene.h"
#include "Scene/Entity.h"        // Entity 类定义
#include "Scene/Components.h"    // ScriptComponent 定义

namespace Redflag {

	namespace Utils {
		static bool IsRedflagClass(MonoClass* monoClass, const char* className)
		{
			if (!monoClass)
				return false;

			const char* nameSpace = mono_class_get_namespace(monoClass);
			const char* name = mono_class_get_name(monoClass);
			return strcmp(nameSpace, "Redflag") == 0 && strcmp(name, className) == 0;
		}

		static ScriptFieldType MonoTypeToScriptFieldType(MonoType* monoType)
		{
			switch (mono_type_get_type(monoType))
			{
				case MONO_TYPE_R4:
					return ScriptFieldType::Float;
				case MONO_TYPE_I4:
					return ScriptFieldType::Int;
				case MONO_TYPE_BOOLEAN:
					return ScriptFieldType::Bool;
				case MONO_TYPE_VALUETYPE:
				{
					MonoClass* fieldClass = mono_type_get_class(monoType);
					if (!fieldClass)
						return ScriptFieldType::None;

					if (IsRedflagClass(fieldClass, "Vector3"))
						return ScriptFieldType::Vector3;

					return ScriptFieldType::None;
				}
				case MONO_TYPE_CLASS:
				{
					MonoClass* fieldClass = mono_type_get_class(monoType);
					if (IsRedflagClass(fieldClass, "Entity"))
						return ScriptFieldType::Entity;

					return ScriptFieldType::None;
				}
			}

			return ScriptFieldType::None;
		}

		static std::string MonoTypeToString(MonoType* monoType)
		{
			char* typeName = mono_type_get_name(monoType);
			std::string result = typeName ? typeName : "<unknown>";
			mono_free(typeName);
			return result;
		}

		//TODO:文件系统
		//读取二进制文件（dll）到内存
		static char* ReadBytes(const std::filesystem::path& filepath, uint32_t* outSize)
		{
			std::ifstream stream(filepath, std::ios::binary | std::ios::ate);

			if (!stream)
				return nullptr;

			std::streampos end = stream.tellg();
			stream.seekg(0, std::ios::beg);
			uint32_t size = end - stream.tellg();

			if (size == 0)
				return nullptr;

			char* buffer = new char[size];
			stream.read((char*)buffer, size);
			stream.close();

			*outSize = size;
			return buffer;
		}

		static MonoAssembly* LoadMonoAssembly(const std::filesystem::path& assemblyPath)
		{
			//加载dll
			uint32_t fileSize = 0;
			char* fileData = ReadBytes(assemblyPath, &fileSize);

			if (!fileData || fileSize == 0)
			{
				RF_CORE_ERROR("Failed to read script assembly: {0}", assemblyPath.string());
				return nullptr;
			}

			//根据dll返回一个 MonoImage* 句柄
			//（后续所有对程序集的元数据读取（类、方法）都依赖这个句柄）
			MonoImageOpenStatus status;
			MonoImage* image = mono_image_open_from_data_full(fileData, fileSize, 1, &status, 0);

			if (status != MONO_IMAGE_OK)
			{
				const char* errorMessage = mono_image_strerror(status);
				// 这里应该记录错误或者输出错误信息
				RF_CORE_ERROR("Failed to load assembly: {0}", errorMessage);
				delete[] fileData;  // 不要忘了释放内存
				return nullptr;
			}

			// 从 Image 创建 Assembly（可执行的程序集）
			std::string pathString = assemblyPath.string();
			MonoAssembly* assembly = mono_assembly_load_from_full(image, pathString.c_str(), &status, 0);
			mono_image_close(image);

			delete[] fileData;
			return assembly;
		}

		void PrintAssemblyTypes(MonoAssembly* assembly)
		{
			MonoImage* image = mono_assembly_get_image(assembly);

			//元数据表
			const MonoTableInfo* typeDefinitionsTable = mono_image_get_table_info(image, MONO_TABLE_TYPEDEF);
			int32_t numTypes = mono_table_info_get_rows(typeDefinitionsTable);

			//遍历所有 namespace.name
			for (int32_t i = 0; i < numTypes; i++)
			{
				uint32_t cols[MONO_TYPEDEF_SIZE];
				// 解码第 i 行的所有列到 cols 数组中
				mono_metadata_decode_row(typeDefinitionsTable, i, cols, MONO_TYPEDEF_SIZE);

				// cols[MONO_TYPEDEF_NAME]       → 类型名称在字符串堆中的索引
				// cols[MONO_TYPEDEF_NAMESPACE]  → 命名空间名称在字符串堆中的索引
				const char* name = mono_metadata_string_heap(image, cols[MONO_TYPEDEF_NAME]);
				const char* nameSpace = mono_metadata_string_heap(image, cols[MONO_TYPEDEF_NAMESPACE]);

				RF_CORE_TRACE("{}.{}", nameSpace, name);
			}
		}
	}

	struct ScriptEngineData
	{
		MonoDomain* RootDomain = nullptr;
		MonoDomain* AppDomain = nullptr;

		MonoAssembly* CoreAssembly = nullptr;
		MonoImage* CoreAssemblyImage = nullptr;
		MonoAssembly* AppAssembly = nullptr;
		MonoImage* AppAssemblyImage = nullptr;

		ScriptClass EntityClass;
		MonoClassField* EntityIDField = nullptr;
		MonoMethod* EntityConstructor = nullptr;

		std::unordered_map<std::string, Ref<ScriptClass>> EntityClasses;

		std::unordered_map<UUID, Ref<ScriptInstance>> EntityInstances;

		// 编辑器模式下用户填的字段默认值，进入 Play 时复制到 ScriptInstance
		std::unordered_map<UUID, ScriptEngine::EntityScriptFieldMap> ScriptFieldCache;

		//运行时
		Scene* SceneContext = nullptr;
		std::string AssemblyStatusMessage;

	};

	static ScriptEngineData* s_Data;

	namespace
	{
		static const std::filesystem::path s_CoreAssemblyPath = "Resources/Scripts/Redflag-ScriptCore.dll";
		static const std::filesystem::path s_AppAssemblyPath = "Resources/Scripts/Sandbox.dll";

		static void ResetAssemblyData()
		{
			s_Data->CoreAssembly = nullptr;
			s_Data->CoreAssemblyImage = nullptr;
			s_Data->AppAssembly = nullptr;
			s_Data->AppAssemblyImage = nullptr;
			s_Data->EntityClass = ScriptClass();
			s_Data->EntityIDField = nullptr;
			s_Data->EntityConstructor = nullptr;
			s_Data->EntityClasses.clear();
			s_Data->EntityInstances.clear();
		}

		static void DestroyAppDomain()
		{
			if (s_Data->AppDomain)
			{
				if (s_Data->RootDomain)
					mono_domain_set(s_Data->RootDomain, false);

				mono_domain_unload(s_Data->AppDomain);
				s_Data->AppDomain = nullptr;
			}

			ResetAssemblyData();
		}
	}





	void ScriptEngine::Init()
	{
		s_Data = new ScriptEngineData();

		//初始化RootDomain
		InitMono();
		const bool assembliesLoaded = LoadAssemblies();
		RF_CORE_ASSERT(assembliesLoaded, s_Data->AssemblyStatusMessage.c_str());


#if 0
		//获取和C#类(存储在s_Data,防止重复构建）
		s_Data->EntityClass = ScriptClass(s_Data->CoreAssemblyImage, "Redflag", "Entity");

		MonoObject* instance = s_Data->EntityClass.Instantiate();

		//调用C#函数
		MonoMethod* printMessageFunc = s_Data->EntityClass.GetMethod("PrintMessage", 0);
		s_Data->EntityClass.InvokeMethod(instance, printMessageFunc, nullptr);

		//调用C#函数（带参数）
		MonoMethod* printIntFunc = s_Data->EntityClass.GetMethod("PrintInt", 1);
		int value = 5;
		void* param = &value;
		s_Data->EntityClass.InvokeMethod(instance, printIntFunc, &param);

		MonoMethod* printIntsFunc = s_Data->EntityClass.GetMethod("PrintInts", 2);
		int value2 = 508;
		void* params[2] =
		{
			&value,
			&value2
		};
		s_Data->EntityClass.InvokeMethod(instance, printIntsFunc, params);

		MonoString* monoString = mono_string_new(s_Data->AppDomain, "Hello World From C++!");
		MonoMethod* printCustomMessageFunc = s_Data->EntityClass.GetMethod("PrintCustomMessage", 1);
		void* stringParam = monoString;
		s_Data->EntityClass.InvokeMethod(instance, printCustomMessageFunc, &stringParam);
#endif
	}


	void ScriptEngine::Shutdown()
	{
		ShutdownMono();      // 先关 Mono
		ResetAssemblyData();
		delete s_Data;       // 再删数据容器
		s_Data = nullptr;
	}

	bool ScriptEngine::IsRuntimeActive()
	{
		return s_Data && s_Data->SceneContext != nullptr;
	}

	bool ScriptEngine::UnloadAssemblies()
	{
		if (!s_Data)
			return false;

		if (IsRuntimeActive() || !s_Data->EntityInstances.empty())
		{
			s_Data->AssemblyStatusMessage = "Stop runtime before unloading script assemblies";
			return false;
		}

		DestroyAppDomain();
		s_Data->AssemblyStatusMessage = "Script assemblies unloaded";
		return true;
	}

	bool ScriptEngine::LoadAssemblies()
	{
		if (!s_Data)
			return false;

		if (!s_Data->RootDomain)
		{
			s_Data->AssemblyStatusMessage = "Mono root domain is not initialized";
			return false;
		}

		if (s_Data->AppDomain)
			DestroyAppDomain();

		//加载核心脚本 API 程序集
		s_Data->CoreAssembly = LoadAssembly(s_CoreAssemblyPath);
		if (!s_Data->CoreAssembly)
		{
			DestroyAppDomain();
			s_Data->AssemblyStatusMessage = "Failed to load script core assembly Redflag-ScriptCore.dll";
			return false;
		}

		//从核心程序集获取可执行的元数据视图
		s_Data->CoreAssemblyImage = mono_assembly_get_image(s_Data->CoreAssembly);
		if (!s_Data->CoreAssemblyImage)
		{
			DestroyAppDomain();
			s_Data->AssemblyStatusMessage = "Failed to get image for Redflag-ScriptCore.dll";
			return false;
		}

		//缓存C#基类
		s_Data->EntityClass = ScriptClass(s_Data->CoreAssemblyImage, "Redflag", "Entity");
		if (!s_Data->EntityClass.IsValid())
		{
			DestroyAppDomain();
			s_Data->AssemblyStatusMessage = "Failed to find script base class Redflag.Entity";
			return false;
		}

		s_Data->EntityIDField = mono_class_get_field_from_name(s_Data->EntityClass.m_MonoClass, "ID");
		s_Data->EntityConstructor = s_Data->EntityClass.GetMethod(".ctor", 1);
		if (!s_Data->EntityIDField || !s_Data->EntityConstructor)
		{
			DestroyAppDomain();
			s_Data->AssemblyStatusMessage = "Failed to find Redflag.Entity ID field or constructor";
			return false;
		}

		//加载游戏脚本程序集
		s_Data->AppAssembly = LoadAssembly(s_AppAssemblyPath);
		if (!s_Data->AppAssembly)
		{
			DestroyAppDomain();
			s_Data->AssemblyStatusMessage = "Failed to load app assembly Sandbox.dll";
			return false;
		}

		s_Data->AppAssemblyImage = mono_assembly_get_image(s_Data->AppAssembly);
		if (!s_Data->AppAssemblyImage)
		{
			DestroyAppDomain();
			s_Data->AssemblyStatusMessage = "Failed to get image for Sandbox.dll";
			return false;
		}

		//遍历游戏程序集中的所有类型，找到 Entity 子类并存入 map(EntityClasses)
		LoadAssemblyClasses(s_Data->AppAssembly);

		// 注册 internal calls(上下文是AppDomain)
		ScriptGlue::RegisterFunctions();
		s_Data->AssemblyStatusMessage = fmt::format("Loaded {0} script classes", s_Data->EntityClasses.size());
		return true;
	}

	bool ScriptEngine::ReloadAssemblies()
	{
		if (!UnloadAssemblies())
			return false;

		return LoadAssemblies();
	}

	const std::string& ScriptEngine::GetAssemblyStatusMessage()
	{
		static const std::string emptyMessage;
		return s_Data ? s_Data->AssemblyStatusMessage : emptyMessage;
	}

	MonoAssembly* ScriptEngine::LoadAssembly(const std::filesystem::path& filepath)
	{
		if (!s_Data->AppDomain)
		{
			//脚本系统 dll 统一运行在同一个 AppDomain
			s_Data->AppDomain = mono_domain_create_appdomain("RedflagScriptRuntime", nullptr);
			RF_CORE_ASSERT(s_Data->AppDomain, "Failed to create script app domain");
		}

		mono_domain_set(s_Data->AppDomain, true);

		MonoAssembly* assembly = Utils::LoadMonoAssembly(filepath);
		if (!assembly)
			RF_CORE_ERROR("Failed to load script assembly {0}", filepath.string());

		return assembly;
	}

	void ScriptEngine::OnRuntimeStart(Scene* scene)
	{
		s_Data->SceneContext = scene;
	}

	void ScriptEngine::OnRuntimeStop()
	{
		s_Data->SceneContext = nullptr;
		s_Data->EntityInstances.clear();
	}

	bool ScriptEngine::EntityClassExists(const std::string& fullClassName)
	{
		return s_Data->EntityClasses.find(fullClassName) != s_Data->EntityClasses.end();
	}

	void ScriptEngine::OnCreateEntity(Entity entity)
	{
		const auto& sc = entity.GetComponent<ScriptComponent>();
		if (ScriptEngine::EntityClassExists(sc.ClassName))
		{
			//创建脚本实例并存入表
			const Ref<ScriptClass>& scriptClass = s_Data->EntityClasses[sc.ClassName];
			Ref<ScriptInstance> instance = CreateRef<ScriptInstance>(scriptClass, entity);
			s_Data->EntityInstances[entity.GetUUID()] = instance;

			// 把编辑器面板里设置的字段默认值应用到实例
			// 必须在 InvokeOnCreate 之前执行，否则用户在 OnCreate 里读到的就是未覆盖的旧值
			auto cacheIt = s_Data->ScriptFieldCache.find(entity.GetUUID());
			if (cacheIt != s_Data->ScriptFieldCache.end())
			{
				for (const auto& [fieldName, bytes] : cacheIt->second)
				{
					if (bytes.empty())
						continue;
					if (const ScriptField* field = scriptClass->GetField(fieldName))
					{
						if (bytes.size() != ScriptFieldTypeSize(field->Type))
							continue;
						instance->SetFieldValue(*field, bytes.data());
					}
				}
			}

			//调用函数
			instance->InvokeOnCreate();
		}
	}

	void ScriptEngine::OnUpdateEntity(Entity entity,float ts)
	{
		UUID entityUUID = entity.GetUUID();
		auto instanceIt = s_Data->EntityInstances.find(entityUUID);
		if (instanceIt == s_Data->EntityInstances.end())
			return;

		Ref<ScriptInstance> instance = instanceIt->second;

		instance->InvokeOnUpdate(ts);
	}

	void ScriptEngine::OnDestroyEntity(UUID entityUUID)
	{
		if (!s_Data)
			return;

		s_Data->EntityInstances.erase(entityUUID);
		s_Data->ScriptFieldCache.erase(entityUUID);

		const uint64_t deletedEntityID = entityUUID;
		for (auto& [ownerEntityID, fieldCache] : s_Data->ScriptFieldCache)
		{
			for (auto& [fieldName, bytes] : fieldCache)
			{
				if (bytes.size() != sizeof(uint64_t))
					continue;

				bool isEntityField = false;
				for (const auto& [className, scriptClass] : s_Data->EntityClasses)
				{
					const ScriptField* field = scriptClass->GetField(fieldName);
					if (field && field->Type == ScriptFieldType::Entity)
					{
						isEntityField = true;
						break;
					}
				}

				if (!isEntityField)
					continue;

				uint64_t referencedEntityID = 0;
				memcpy(&referencedEntityID, bytes.data(), sizeof(referencedEntityID));
				if (referencedEntityID == deletedEntityID)
				{
					referencedEntityID = 0;
					memcpy(bytes.data(), &referencedEntityID, sizeof(referencedEntityID));
				}
			}
		}
	}

	Scene* ScriptEngine::GetSceneContext()
	{
		return s_Data->SceneContext;
	}

	Ref<ScriptInstance> ScriptEngine::GetEntityScriptInstance(UUID entityUUID)
	{
		auto it = s_Data->EntityInstances.find(entityUUID);
		if (it == s_Data->EntityInstances.end())
			return nullptr;

		return it->second;
	}

	std::unordered_map<std::string, Ref<ScriptClass>> ScriptEngine::GetEntityClasses()
	{
		return s_Data->EntityClasses;
	}

	ScriptEngine::EntityScriptFieldMap& ScriptEngine::GetScriptFieldCache(UUID entityUUID)
	{
		// operator[] 不存在则插入空 map，符合"按需创建"的语义
		return s_Data->ScriptFieldCache[entityUUID];
	}

	size_t ScriptFieldTypeSize(ScriptFieldType type)
	{
		// Mono 中 bool 占 1 字节，int 4 字节，float 4 字节，glm::vec3 与 C# Vector3 同布局 12 字节
		switch (type)
		{
			case ScriptFieldType::Float:   return sizeof(float);
			case ScriptFieldType::Int:     return sizeof(int32_t);
			case ScriptFieldType::Bool:    return sizeof(bool);
			case ScriptFieldType::Vector3: return sizeof(glm::vec3);
			case ScriptFieldType::Entity:  return sizeof(uint64_t);
			default:                       return 0;
		}
	}

	void ScriptEngine::ShutdownMono()
	{
		if (s_Data->AppDomain)
		{
			DestroyAppDomain();
		}
		if (s_Data->RootDomain)
		{
			mono_jit_cleanup(s_Data->RootDomain);
			s_Data->RootDomain = nullptr;
		}
		s_Data->SceneContext = nullptr;
	}




	void ScriptEngine::InitMono()
	{
		//加载程序集
		//去 mono/lib 文件夹找 C# 的核心系统库(如mscorlib.dll、System.dll）
		mono_set_assemblies_path("mono/lib");

		//创建RootDomain
		//Mono虚拟机所在
		MonoDomain* rootDomain = mono_jit_init("RedflagJITRuntime");
		RF_CORE_ASSERT(rootDomain);

		s_Data->RootDomain = rootDomain;
	}

	MonoObject* ScriptEngine::InstantiateClass(MonoClass* monoClass)
	{
		//在AppDomain上分配空间
		MonoObject* instance = mono_object_new(s_Data->AppDomain, monoClass);
		//真正实例化
		mono_runtime_object_init(instance);

		return instance;
	}

	void ScriptEngine::LoadAssemblyClasses(MonoAssembly* assembly)
	{
		s_Data->EntityClasses.clear();
		MonoImage* image = mono_assembly_get_image(assembly);

		//元数据表
		const MonoTableInfo* typeDefinitionsTable = mono_image_get_table_info(image, MONO_TABLE_TYPEDEF);
		int32_t numTypes = mono_table_info_get_rows(typeDefinitionsTable);

		MonoClass* entityClass = mono_class_from_name(s_Data->CoreAssemblyImage, "Redflag", "Entity");
		RF_CORE_ASSERT(entityClass, "Failed to find script base class Redflag.Entity while loading assembly classes");

		//遍历游戏脚本程序集中的所有类型
		for (int32_t i = 0; i < numTypes; i++)
		{
			uint32_t cols[MONO_TYPEDEF_SIZE];
			mono_metadata_decode_row(typeDefinitionsTable, i, cols, MONO_TYPEDEF_SIZE);

			const char* name = mono_metadata_string_heap(image, cols[MONO_TYPEDEF_NAME]);        // 类名，如 "Entity"
			const char* nameSpace = mono_metadata_string_heap(image, cols[MONO_TYPEDEF_NAMESPACE]); // 命名空间，如 "Redflag"

			std::string fullName;
			if (strlen(nameSpace) != 0)              // 如果有命名空间
				fullName = fmt::format("{}.{}", nameSpace, name);  // → "Redflag.Entity"
			else                                      // 如果没有命名空间（全局类型）
				fullName = name;                      // → "Entity"

			//找到Entity的子类并存入Map
			MonoClass* monoClass = mono_class_from_name(image, nameSpace, name);
			if (monoClass == nullptr)
				continue;

			bool isEntity = mono_class_is_subclass_of(monoClass, entityClass, false);
			if (isEntity)
			{
				Ref<ScriptClass> scriptClass = CreateRef<ScriptClass>(image, nameSpace, name);
				LoadClassFields(scriptClass);
				s_Data->EntityClasses[fullName] = scriptClass;
			}
		}
	}

	void ScriptEngine::LoadClassFields(const Ref<ScriptClass>& scriptClass)
	{
		RF_CORE_ASSERT(scriptClass && scriptClass->m_MonoClass, "Cannot load fields for an invalid script class");

		scriptClass->m_Fields.clear();

		void* iterator = nullptr;
		while (MonoClassField* field = mono_class_get_fields(scriptClass->m_MonoClass, &iterator))
		{
			const uint32_t flags = mono_field_get_flags(field);
			const bool isPublic = (flags & MONO_FIELD_ATTR_FIELD_ACCESS_MASK) == MONO_FIELD_ATTR_PUBLIC;
			const bool isStatic = (flags & MONO_FIELD_ATTR_STATIC) != 0;

			if (!isPublic || isStatic)
				continue;

			MonoType* monoType = mono_field_get_type(field);
			ScriptFieldType fieldType = Utils::MonoTypeToScriptFieldType(monoType);

			if (fieldType == ScriptFieldType::None)
			{
				RF_CORE_WARN("Skipping unsupported script field {0}.{1} ({2})",
					scriptClass->GetFullName(),
					mono_field_get_name(field),
					Utils::MonoTypeToString(monoType));
				continue;
			}

			ScriptField scriptField;
			scriptField.Name = mono_field_get_name(field);
			scriptField.Type = fieldType;
			scriptField.ClassField = field;

			scriptClass->m_Fields.push_back(scriptField);
		}

		// 临时实例化一次，读出 C# 字段初始化器执行后的值作为默认值
		// 这个对象只用来读默认值，之后由 Mono GC 自动回收
		if (!scriptClass->m_Fields.empty())
		{
			MonoObject* defaultsInstance = InstantiateClass(scriptClass->m_MonoClass);
			if (defaultsInstance)
			{
				for (ScriptField& f : scriptClass->m_Fields)
				{
					const size_t typeSize = ScriptFieldTypeSize(f.Type);
					if (typeSize == 0)
						continue;
					f.DefaultValue.assign(typeSize, 0);

					if (f.Type == ScriptFieldType::Entity)
					{
						MonoObject* entityObject = nullptr;
						mono_field_get_value(defaultsInstance, f.ClassField, &entityObject);

						uint64_t entityID = 0;
						if (entityObject && s_Data->EntityIDField)
							mono_field_get_value(entityObject, s_Data->EntityIDField, &entityID);

						memcpy(f.DefaultValue.data(), &entityID, sizeof(entityID));
					}
					else
					{
						mono_field_get_value(defaultsInstance, f.ClassField, f.DefaultValue.data());
					}
				}
			}
		}
	}


	ScriptClass::ScriptClass(MonoImage* image, const std::string& classNamespace, const std::string& className)
		:m_ClassNamespace(classNamespace), m_ClassName(className)
	{
		m_MonoClass = mono_class_from_name(image, classNamespace.c_str(), className.c_str());

		if (!m_MonoClass)
		{
			std::string fullClassName = classNamespace.empty() ? className : fmt::format("{0}.{1}", classNamespace, className);
			RF_CORE_ERROR("Failed to find script class {0}", fullClassName);
		}
	}

	std::string ScriptClass::GetFullName() const
	{
		return m_ClassNamespace.empty() ? m_ClassName : fmt::format("{0}.{1}", m_ClassNamespace, m_ClassName);
	}

	const ScriptField* ScriptClass::GetField(const std::string& name) const
	{
		for (const ScriptField& field : m_Fields)
		{
			if (field.Name == name)
				return &field;
		}

		return nullptr;
	}

	MonoObject* ScriptClass::Instantiate()
	{
		RF_CORE_ASSERT(m_MonoClass, "Cannot instantiate an invalid script class");
		return ScriptEngine::InstantiateClass(m_MonoClass);
	}


	MonoMethod* ScriptClass::GetMethod(const std::string& name, int parameterCount)
	{
		RF_CORE_ASSERT(m_MonoClass, "Cannot fetch method from an invalid script class");
		return mono_class_get_method_from_name(m_MonoClass, name.c_str(), parameterCount);
	}

	MonoObject* ScriptClass::InvokeMethod(MonoObject* instance, MonoMethod* method, void** params)
	{
		return mono_runtime_invoke(method, instance, params, nullptr);
	}

	ScriptInstance::ScriptInstance(Ref<ScriptClass> ScriptClass,Entity entity)
		:m_ScriptClass(ScriptClass)
	{
		//在AppDomain上分配MonoObject内存
		m_Instance = ScriptClass->Instantiate();
		
		//获取方法并存入函数指针
		m_Constructor = s_Data->EntityClass.GetMethod(".ctor", 1);
		m_OnCreateMethod = ScriptClass->GetMethod("OnCreate", 0);
		m_OnUpdateMethod = ScriptClass->GetMethod("OnUpdate", 1);
		
		//调用实体的C#构造函数，传入UUID
		uint64_t entityID = entity.GetUUID();
		void* param = &entityID;
		m_ScriptClass->InvokeMethod(m_Instance, m_Constructor, &param);
	}

	bool ScriptInstance::GetFieldValue(const ScriptField& field, void* buffer) const
	{
		if (!m_Instance || !field.ClassField || !buffer)
			return false;

		if (field.Type == ScriptFieldType::Entity)
		{
			MonoObject* entityObject = nullptr;
			mono_field_get_value(m_Instance, field.ClassField, &entityObject);

			uint64_t entityID = 0;
			if (entityObject && s_Data->EntityIDField)
				mono_field_get_value(entityObject, s_Data->EntityIDField, &entityID);

			memcpy(buffer, &entityID, sizeof(entityID));
			return true;
		}

		mono_field_get_value(m_Instance, field.ClassField, buffer);
		return true;
	}

	bool ScriptInstance::SetFieldValue(const ScriptField& field, const void* value)
	{
		if (!m_Instance || !field.ClassField || !value)
			return false;

		if (field.Type == ScriptFieldType::Entity)
		{
			uint64_t entityID = 0;
			memcpy(&entityID, value, sizeof(entityID));

			MonoObject* entityObject = nullptr;
			if (entityID != 0)
			{
				bool entityExists = true;
				if (s_Data->SceneContext)
					entityExists = (bool)s_Data->SceneContext->GetEntityByUUID(UUID(entityID));

				if (entityExists)
				{
					entityObject = s_Data->EntityClass.Instantiate();
					void* param = &entityID;
					s_Data->EntityClass.InvokeMethod(entityObject, s_Data->EntityConstructor, &param);
				}
			}

			mono_field_set_value(m_Instance, field.ClassField, entityObject);
			return true;
		}

		mono_field_set_value(m_Instance, field.ClassField, const_cast<void*>(value));
		return true;
	}

	//引擎创建完所有实体统一调用
	void ScriptInstance::InvokeOnCreate()
	{
		if (!m_OnCreateMethod)
			return;

		m_ScriptClass->InvokeMethod(m_Instance, m_OnCreateMethod,nullptr);
	}

	void ScriptInstance::InvokeOnUpdate(float ts)
	{
		if (!m_OnUpdateMethod)
			return;

		void* param = &ts;

		m_ScriptClass->InvokeMethod(m_Instance, m_OnUpdateMethod, &param);
	}

}