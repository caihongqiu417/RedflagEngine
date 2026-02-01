#pragma once

// DLL 宏定义 - 修改为静态库版本
#ifdef RF_PLATFORM_WINDOWS
    // 如果是静态库，不需要导出/导入声明
#ifdef RF_BUILD_DLL
#define REDFLAG_API __declspec(dllexport)  // 构建DLL时导出
#elif defined(RF_DYNAMIC_LINK)
#define REDFLAG_API __declspec(dllimport)  // 动态链接时导入
#else
#define REDFLAG_API  // 静态库：什么都不需要
#endif
#else
#error Redflag only support Windows!
#endif

// 断言系统
#ifdef RF_ENABLE_ASSERTS
#define RF_CLIENT_ASSERT(x, ...) { if(!(x)) { RF_CLIENT_ERROR("Assertion Failed: {0}", __VA_ARGS__); __debugbreak(); } }
#define RF_CORE_ASSERT(x, ...) { if(!(x)) { RF_CORE_ERROR("Assertion Failed: {0}", __VA_ARGS__); __debugbreak(); } }
#else
#define RF_CLIENT_ASSERT(x, ...)
#define RF_CORE_ASSERT(x, ...)
#endif

#define BIT(x) (1 << (x))

// 事件绑定宏
#define RF_BIND_EVENT_FN(fn) std::bind(&fn, this, std::placeholders::_1)

//智能指针
namespace Redflag {

    template<typename T>
    using Scope = std::unique_ptr<T>;

    template<typename T>
    using Ref = std::shared_ptr<T>;
}