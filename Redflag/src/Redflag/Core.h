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