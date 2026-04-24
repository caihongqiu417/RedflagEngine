#include "rfpch.h"
#include "ScriptEngine.h"

#include "mono/jit/jit.h"
#include"mono/metadata/assembly.h"
#include"glm/glm.hpp"
#include"mono/metadata/object.h"
#include"ScriptGlue.h"

namespace Redflag {

	namespace Utils {
		//TODO:文件系统
		//读取二进制文件（dll)到内存
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
	}

	struct ScriptEngineData
	{
		MonoDomain* RootDomain = nullptr;
		MonoDomain* AppDomain = nullptr;

		MonoAssembly* CoreAssembly = nullptr;
		MonoImage* CoreAssemblyImage = nullptr;

		ScriptClass EntityClass;
	};

	static ScriptEngineData* s_Data;


	


	void ScriptEngine::Init()
	{
		s_Data = new ScriptEngineData();

		//初始化RootDomain
		InitMono();

		//初始化AppDomain
		LoadAssembly("Resources/Scripts/Redflag-ScriptCore.dll");

		// 注册 internal calls(上下文是AppDomain)
		ScriptGlue::RegisterFunctions();

		//从 Assembly 获取可执行的元数据视图
		s_Data->CoreAssemblyImage = mono_assembly_get_image(s_Data->CoreAssembly);

		//获取和C#类(存储在s_Data,防止重复构建）
		s_Data->EntityClass = ScriptClass("Redflag", "Entity");
		
		MonoObject* instance = s_Data->EntityClass.Instantiate();

		//调用C#函数
		MonoMethod* printMessageFunc = s_Data->EntityClass.GetMethod("PrintMessage",0);
		s_Data->EntityClass.InvokeMethod(instance,printMessageFunc,nullptr);

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
		s_Data->EntityClass.InvokeMethod( instance, printCustomMessageFunc,&stringParam);
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
		//脚本系统所在（热更新）
		s_Data->AppDomain = mono_domain_create_appdomain("RedflagScriptRuntime", nullptr);
		mono_domain_set(s_Data->AppDomain, true);


		//加载自己游戏的C#脚本（已经通过premake编译为dll）
		//自动加载到AppDomain
		s_Data->CoreAssembly = Utils::LoadMonoAssembly(filepath);
		//PrintAssemblyTypes(s_Data->CoreAssembly);
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

	void PrintAssemblyTypes(MonoAssembly* assembly)
	{
		MonoImage* image = mono_assembly_get_image(assembly);

		//元数据表
		const MonoTableInfo* typeDefinitionsTable = mono_image_get_table_info(image, MONO_TABLE_TYPEDEF);
		int32_t numTypes = mono_table_info_get_rows(typeDefinitionsTable);

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


	ScriptClass::ScriptClass(const std::string& classNamespace, const std::string& className)
		:m_ClassNamespace(classNamespace), m_ClassName(className)
	{
		m_MonoClass = mono_class_from_name(s_Data->CoreAssemblyImage, classNamespace.c_str(), className.c_str());
	}

	MonoObject* ScriptClass::Instantiate()
	{
		return ScriptEngine::InstantiateClass(m_MonoClass);
	}

	MonoMethod* ScriptClass::GetMethod(const std::string& name,int parameterCount)
	{
		return mono_class_get_method_from_name(m_MonoClass, name.c_str(), parameterCount);
	}

	MonoObject* ScriptClass::InvokeMethod(MonoObject* instance,MonoMethod* method, void** params)
	{
		return mono_runtime_invoke(method, instance, params, nullptr);
	}

}