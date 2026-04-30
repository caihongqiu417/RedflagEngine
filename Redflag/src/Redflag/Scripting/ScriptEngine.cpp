#include "rfpch.h"
#include "ScriptEngine.h"

#include "mono/jit/jit.h"
#include"mono/metadata/assembly.h"
#include"glm/glm.hpp"
#include"mono/metadata/object.h"
#include"ScriptGlue.h"

#include"Scene/Scene.h"
#include "Scene/Entity.h"        // Entity 类定义
#include "Scene/Components.h"    // ScriptComponent 定义

namespace Redflag {

	namespace Utils {
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

		ScriptClass EntityClass;

		std::unordered_map<std::string, Ref<ScriptClass>> EntityClasses;

		std::unordered_map<UUID, Ref<ScriptInstance>> EntityInstances;

		//运行时
		Scene* SceneContext = nullptr;

	};

	static ScriptEngineData* s_Data;





	void ScriptEngine::Init()
	{
		s_Data = new ScriptEngineData();

		//初始化RootDomain
		InitMono();

		//初始化AppDomain
		LoadAssembly("Resources/Scripts/Redflag-ScriptCore.dll");

		//从 Assembly 获取可执行的元数据视图
		s_Data->CoreAssemblyImage = mono_assembly_get_image(s_Data->CoreAssembly);

		//缓存C#基类
		s_Data->EntityClass = ScriptClass("Redflag", "Entity");
		RF_CORE_ASSERT(s_Data->EntityClass.m_MonoClass, "Failed to find script base class Redflag.Entity");

		//遍历所有类型，找到Entity子类并存入map(EntityClasses)
		LoadAssemblyClasses(s_Data->CoreAssembly);

		// 注册 internal calls(上下文是AppDomain)
		ScriptGlue::RegisterFunctions();


#if 0
		//获取和C#类(存储在s_Data,防止重复构建）
		s_Data->EntityClass = ScriptClass("Redflag", "Entity");

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
		delete s_Data;       // 再删数据容器
		s_Data = nullptr;
	}

	void ScriptEngine::LoadAssembly(const std::filesystem::path& filepath)
	{
		//创建AppDomain
		//脚本系统dll（热更新）
		s_Data->AppDomain = mono_domain_create_appdomain("RedflagScriptRuntime", nullptr);
		mono_domain_set(s_Data->AppDomain, true);


		//加载自己游戏的C#脚本（已经通过premake编译为dll）
		//自动加载到AppDomain
		s_Data->CoreAssembly = Utils::LoadMonoAssembly(filepath);
		//PrintAssemblyTypes(s_Data->CoreAssembly);
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
			Ref<ScriptInstance> instance=CreateRef<ScriptInstance>(s_Data->EntityClasses[sc.ClassName],entity);
			s_Data->EntityInstances[entity.GetUUID()] = instance;

			//调用函数
			instance->InvokeOnCreate();
		}
	}

	void ScriptEngine::OnUpdateEntity(Entity entity,float ts)
	{
		UUID entityUUID = entity.GetUUID();
		RF_CORE_ASSERT(s_Data->EntityInstances.find(entityUUID) != s_Data->EntityInstances.end());

		Ref<ScriptInstance> instance = s_Data->EntityInstances[entityUUID];

		instance->InvokeOnUpdate(ts);
	}

	Scene* ScriptEngine::GetSceneContext()
	{
		return s_Data->SceneContext;
	}

	std::unordered_map<std::string, Ref<ScriptClass>> ScriptEngine::GetEntityClasses()
	{
		return s_Data->EntityClasses;
	}

	void ScriptEngine::ShutdownMono()
	{
		if (s_Data->AppDomain)
		{
			mono_domain_unload(s_Data->AppDomain);
			s_Data->AppDomain = nullptr;
		}
		if (s_Data->RootDomain)
		{
			mono_jit_cleanup(s_Data->RootDomain);
			s_Data->RootDomain = nullptr;
		}
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

		MonoClass* entityClass = mono_class_from_name(image, "Redflag", "Entity");
		RF_CORE_ASSERT(entityClass, "Failed to find script base class Redflag.Entity while loading assembly classes");

		//遍历Redflag-ScriptCore.dll 中定义的所有类型
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
				s_Data->EntityClasses[fullName] = CreateRef <ScriptClass>(nameSpace, name);
		}
	}


	ScriptClass::ScriptClass(const std::string& classNamespace, const std::string& className)
		:m_ClassNamespace(classNamespace), m_ClassName(className)
	{
		m_MonoClass = mono_class_from_name(s_Data->CoreAssemblyImage, classNamespace.c_str(), className.c_str());

		if (!m_MonoClass)
		{
			std::string fullClassName = classNamespace.empty() ? className : fmt::format("{0}.{1}", classNamespace, className);
			RF_CORE_ERROR("Failed to find script class {0}", fullClassName);
		}
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
		UUID entityID = entity.GetUUID();
		void* param = &entityID;
		m_ScriptClass->InvokeMethod(m_Instance, m_Constructor, &param);
	}

	//引擎创建完所有实体统一调用
	void ScriptInstance::InvokeOnCreate()
	{
		m_ScriptClass->InvokeMethod(m_Instance, m_OnCreateMethod,nullptr);
	}

	void ScriptInstance::InvokeOnUpdate(float ts)
	{
		void* param = &ts;

		m_ScriptClass->InvokeMethod(m_Instance, m_OnUpdateMethod, &param);
	}

}