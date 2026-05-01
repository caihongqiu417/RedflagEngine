#include "rfpch.h"
#include "ScriptGlue.h"
#include "ScriptEngine.h"
#include "mono/metadata/object.h"
#include <glm/glm.hpp>
#include"Scene/Scene.h"
#include "Scene/Entity.h"
#include"Redflag/Core/Input.h"

#define RF_ADD_INTERNAL_CALL(Name) mono_add_internal_call("Redflag.InternalCalls::" #Name, reinterpret_cast<const void*>(Name))

namespace Redflag {

    static void NativeLog(MonoString* string, int parameter)
    {
        char* cStr = mono_string_to_utf8(string);
        std::string str(cStr);
        mono_free(cStr);
        RF_CORE_INFO("{0}, {1}", str, parameter);
    }

    static void NativeLog_Vector(glm::vec3* parameter, glm::vec3* outResult)
    {
        RF_CORE_WARN("Value: ({0}, {1}, {2})", parameter->x, parameter->y, parameter->z);
        if (outResult != nullptr)
            *outResult = glm::normalize(*parameter);
    }

    static void TransformComponent_GetTranslation(UUID entityID, glm::vec3* outTranslation)
    {
        Scene* scene = ScriptEngine::GetSceneContext();
        if (!scene || !outTranslation)
            return;

        Entity entity = scene->GetEntityByUUID(entityID);
        if (!entity || !entity.HasComponent<TransformComponent>())
        {
            *outTranslation = glm::vec3(0.0f);
            return;
        }

        *outTranslation = entity.GetComponent<TransformComponent>().Translation;
    }

    static void TransformComponent_SetTranslation(UUID entityID, glm::vec3* translation)
    {
        Scene* scene = ScriptEngine::GetSceneContext();
        if (!scene || !translation)
            return;

        Entity entity = scene->GetEntityByUUID(entityID);
        if (!entity || !entity.HasComponent<TransformComponent>())
            return;

        entity.GetComponent<TransformComponent>().Translation = *translation;
    }

    static bool Input_IsKeyDown(int keycode)
    {
        return Input::IsKeyPressed(keycode);
    }

    void ScriptGlue::RegisterFunctions()
    {
        RF_ADD_INTERNAL_CALL(NativeLog);
        RF_ADD_INTERNAL_CALL(NativeLog_Vector);

        RF_ADD_INTERNAL_CALL(TransformComponent_GetTranslation);
        RF_ADD_INTERNAL_CALL(TransformComponent_SetTranslation);

        RF_ADD_INTERNAL_CALL(Input_IsKeyDown);
    }
}
