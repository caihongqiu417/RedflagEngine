#include "rfpch.h"
#include "ScriptGlue.h"
#include "mono/metadata/object.h"
#include <glm/glm.hpp>

//包装mono_add_internal_call
#define RF_ADD_INTERNAL_CALL(Name) mono_add_internal_call("Redflag.InternalCalls::" #Name, reinterpret_cast<const void*>(Name))

namespace Redflag {

    // called by C#
    static void NativeLog(MonoString* string, int parameter)
    {
        char* cStr = mono_string_to_utf8(string);
        std::string str(cStr);
        mono_free(cStr);

        RF_CORE_INFO("{0}, {1}", str, parameter);
    }

    // called by C# - 注意参数顺序：ref parameter, out outResult
    //有C#创建内存，C++只负责填充
    static void NativeLog_Vector(glm::vec3* parameter, glm::vec3* outResult)
    {
        RF_CORE_WARN("Value: ({0}, {1}, {2})",
            parameter->x,
            parameter->y,
            parameter->z);

        // 计算归一化结果并通过 out 参数返回
        if (outResult != nullptr)
        {
            *outResult = glm::normalize(*parameter);
        }
    }

    //注册internalcall
    void ScriptGlue::RegisterFunctions()  
    {
        // internal call - 注意前两个参数是直接传递，outResult 通过指针返回
        RF_ADD_INTERNAL_CALL(NativeLog);
        RF_ADD_INTERNAL_CALL(NativeLog_Vector);
    }
}