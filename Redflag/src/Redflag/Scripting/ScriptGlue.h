//胶水层
//用于注册internal，供C#侧call

#pragma once

#include "mono/jit/jit.h"           // 添加 mono 头文件
#include "mono/metadata/object.h"   // 添加 mono 对象头文件
#include <glm/glm.hpp>             
#include <iostream>                 

namespace Redflag {

	class ScriptGlue
	{
	public:
		static void RegisterFunctions();
	};
}