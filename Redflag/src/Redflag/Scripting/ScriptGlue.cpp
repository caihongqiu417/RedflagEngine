#include "rfpch.h"
#include "ScriptGlue.h"
#include "ScriptEngine.h"
#include "mono/metadata/object.h"
#include <glm/glm.hpp>
#include"Scene/Scene.h"
#include "Scene/Entity.h" 
#include"Redflag/Core/Input.h"

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

    //组件的Translation返回给C#
    static void Entity_GetTranslation(UUID entityID,glm::vec3* outTranslation)
    {
        Scene* scene = ScriptEngine::GetSceneContext();
        
        Entity entity = scene->GetEntityByUUID(entityID);
        *outTranslation=entity.GetComponent<TransformComponent>().Translation;
    }

    //C#传入参数给组件
    static void Entity_SetTranslation(UUID entityID,glm::vec3* translation)
    {
        Scene* scene = ScriptEngine::GetSceneContext();

        Entity entity = scene->GetEntityByUUID(entityID);
        entity.GetComponent<TransformComponent>().Translation = *translation;
    }

    static bool Input_IsKeyDown(int keycode)
    {
        return Input::IsKeyPressed(keycode);
    }

    //注册internalcall
    void ScriptGlue::RegisterFunctions()  
    {
        // internal call - 注意前两个参数是直接传递，outResult 通过指针返回
        RF_ADD_INTERNAL_CALL(NativeLog);
        RF_ADD_INTERNAL_CALL(NativeLog_Vector);

        RF_ADD_INTERNAL_CALL(Entity_GetTranslation);
        RF_ADD_INTERNAL_CALL(Entity_SetTranslation);

        RF_ADD_INTERNAL_CALL(Input_IsKeyDown);
    }
}