#include "rfpch.h"
#include "ScriptEngine.h"

#include "mono/jit/jit.h"
#include"mono/metadata/assembly.h"
#include"mono/metadata/object.h"

namespace Redflag {

	struct ScriptEngineData
	{
		MonoDomain* RootDomain=nullptr;
		MonoDomain* AppDomain = nullptr;

		MonoAssembly* CoreAssembly=nullptr;

	};

	static ScriptEngineData* s_Data;


	void ScriptEngine::Init()
	{
		s_Data=new ScriptEngineData();

		InitMono();
	}


	void ScriptEngine::Shutdown()
	{
		ShutdownMono();      // 先关 Mono
		delete s_Data;       // 再删数据容器
		s_Data = nullptr;
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

	//读取二进制文件（dll)到内存
	char* ReadBytes(const std::string& filepath, uint32_t* outSize)
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


	//dll->元数据和程序集
	MonoAssembly* LoadCsharpAssembly(const std::string& assemblyPath)
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
			return nullptr;
		}

		// 从 Image 创建 Assembly（可执行的程序集）
		MonoAssembly* assembly = mono_assembly_load_from_full(image, assemblyPath.c_str(), &status, 0);
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

		//创建AppDomain
		//脚本系统所在（热更新）
		s_Data->AppDomain = mono_domain_create_appdomain("RedflagScriptRuntime", nullptr);
		mono_domain_set(s_Data->AppDomain, true);

		//加载自己游戏的C#脚本（已经通过premake编译为dll）
		//自动加载到AppDomain
		s_Data->CoreAssembly=LoadCsharpAssembly("Resources/Scripts/Redflag-ScriptCore.dll");
		PrintAssemblyTypes(s_Data->CoreAssembly);

		//1.创建对象(及调用构造函数）
		//1.1 从 Assembly 获取可执行的元数据视图
		MonoImage* assemblyImage = mono_assembly_get_image(s_Data->CoreAssembly);
		//1.2 找到具体的类
		MonoClass* monoClass = mono_class_from_name(assemblyImage, "Redflag", "Main");
		//1.3 在APPDomain中分配内存
		MonoObject* instance=mono_object_new(s_Data->AppDomain, monoClass);
		//1.4 调用构造函数，真正初始化对象
		mono_runtime_object_init(instance);

		//2.调用函数
		MonoMethod* printMessageFunc=mono_class_get_method_from_name(monoClass, "PrintMessage", 0);
		mono_runtime_invoke(printMessageFunc, instance, nullptr, nullptr);

		//3.调用含参数的函数
		//3.1 在C++栈上创建参数
		MonoMethod* printIntFunc = mono_class_get_method_from_name(monoClass, "PrintInt", 1);
		int value = 5;
		void* param = &value;
		mono_runtime_invoke(printIntFunc, instance, &param, nullptr);

		MonoMethod* printIntsFunc = mono_class_get_method_from_name(monoClass, "PrintInts", 2);
		int value2 = 508;
		void* params[2] =
		{
			&value,
			&value2
		};
		mono_runtime_invoke(printIntsFunc, instance, params, nullptr);

		//3.2 在AppDomain托管堆创建参数
		MonoString* monoString = mono_string_new(s_Data->AppDomain, "Hello World From C++!");
		MonoMethod* printCustomMessageFunc = mono_class_get_method_from_name(monoClass, "PrintCustomMessage", 1);
		void* stringParam = monoString;
		mono_runtime_invoke(printCustomMessageFunc, instance, &stringParam, nullptr);
	}
}