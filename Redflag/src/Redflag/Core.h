//DLL Macros

#pragma once

#ifdef RF_PLATFORM_WINDOWS
	#ifdef RF_BUILD_DLL
		#define REDFLAG_API _declspec(dllexport)
	#else
		#define REDFLAG_API _declspec(dllimport)
	#endif
#else
	#error Redflag only support Windows!
#endif

#ifdef RF_ENABLE_ASSERTS
#define RF_CLIENT_ASSERT(x,...) {if(!(x)){RF_CLIENT_ERROR("¶ÏÑÔ´íÎó£º{0}",__VA_ARGS__);__debugbreak();}}
#define RF_CORE_ASSERT(x,...) {if(!(x)){RF_CORE_ERROR("¶ÏÑÔ´íÎó£º{0}",__VA_ARGS__);__debugbreak();}}
#else
#define RF_CLIENT_ASSERT(x,...)
#define RF_CORE_ASSERT(x,...)

#endif


#define BIT(x)(1<<x)