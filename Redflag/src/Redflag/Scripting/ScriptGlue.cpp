#include "rfpch.h"
#include "ScriptGlue.h"
#include "GameplayBindings.h"
#include "NpcBindings.h"
#include "ScriptEngine.h"
#include "mono/metadata/object.h"
#include <glm/glm.hpp>
#include"Scene/Scene.h"
#include "Scene/Entity.h"
#include"Redflag/Core/Input.h"
#include <imgui.h>

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

    static void TransformComponent_GetRotation(UUID entityID, glm::vec3* outRotation)
    {
        Scene* scene = ScriptEngine::GetSceneContext();
        if (!scene || !outRotation)
            return;

        Entity entity = scene->GetEntityByUUID(entityID);
        if (!entity || !entity.HasComponent<TransformComponent>())
        {
            *outRotation = glm::vec3(0.0f);
            return;
        }

        *outRotation = entity.GetComponent<TransformComponent>().Rotation;
    }

    static void TransformComponent_SetRotation(UUID entityID, glm::vec3* rotation)
    {
        Scene* scene = ScriptEngine::GetSceneContext();
        if (!scene || !rotation)
            return;

        Entity entity = scene->GetEntityByUUID(entityID);
        if (!entity || !entity.HasComponent<TransformComponent>())
            return;

        entity.GetComponent<TransformComponent>().Rotation = *rotation;
    }

    static bool Entity_Exists(UUID id)
    {
        return bool(ScriptEngine::GetSceneContext()->GetEntityByUUID(id));
    }

    static float PointLightComponent_GetIntensity(UUID id)
    {
        Entity light = ScriptEngine::GetSceneContext()->GetEntityByUUID(id);
        return light && light.HasComponent<PointLightComponent>() ? light.GetComponent<PointLightComponent>().Intensity : 0.f;
    }

    static bool TerrainComponent_GetSurface(UUID id, float x, float z, float* height, glm::vec3* normal)
    {
        *height = 0.f;
        *normal = glm::vec3(0);
        Entity entity = ScriptEngine::GetSceneContext()->GetEntityByUUID(id);
        if (!entity || !entity.HasComponent<TerrainComponent>()) return false;

        const TerrainComponent& ground = entity.GetComponent<TerrainComponent>();
        const Ref<Terrain> map = ground.GetTerrain();
        if (!map) return false;
        const auto surface = map->SampleSurface(x, z, ground.CellSize, ground.HeightScale);
        if (!surface) return false;

        *height = surface->Height;
        *normal = surface->Normal;
        return true;
    }

    static void PointLightComponent_SetIntensity(UUID id, float value)
    {
        Entity light = ScriptEngine::GetSceneContext()->GetEntityByUUID(id);
        if (light && light.HasComponent<PointLightComponent>())
            light.GetComponent<PointLightComponent>().Intensity = std::max(value, 0.f);
    }

    static MonoObject* Entity_GetScript(UUID id)
    {
        Ref<ScriptInstance> script = ScriptEngine::GetEntityScriptInstance(id);
        return script ? script->GetManagedObject() : nullptr;
    }

    static bool SkeletonAnimationComponent_Move(UUID id, const glm::vec3* direction)
    {
        return ScriptEngine::GetSceneContext()->SetCharacterMovement(id, {direction->x, direction->z});
    }

    static bool SkeletonAnimationComponent_Face(UUID id, const glm::vec3* direction)
    {
        return ScriptEngine::GetSceneContext()->FaceCharacter(id, {direction->x, direction->z});
    }

    static bool Input_IsKeyDown(int keycode)
    {
        // 给队友打字时，W、E 等字母只进入输入框。
        if (ImGui::GetCurrentContext() && ImGui::GetIO().WantTextInput) return false;
        return Input::IsKeyPressed(keycode);
    }

    static bool Input_IsMouseButtonDown(int button) { return Input::IsMouseButtonPressed(button); }

    static void Input_CaptureCursor(bool capture) { Input::CaptureCursor(capture); }

    static void Input_MousePosition(float* x, float* y)
    {
        const auto [mouseX, mouseY] = Input::GetMousePosition();
        *x = mouseX;
        *y = mouseY;
    }

    void ScriptGlue::RegisterFunctions()
    {
        RegisterGameplayCalls();
        RegisterNpcCalls();
        RF_ADD_INTERNAL_CALL(TerrainComponent_GetSurface);
        RF_ADD_INTERNAL_CALL(PointLightComponent_GetIntensity);
        RF_ADD_INTERNAL_CALL(PointLightComponent_SetIntensity);
        RF_ADD_INTERNAL_CALL(NativeLog);
        RF_ADD_INTERNAL_CALL(NativeLog_Vector);

        RF_ADD_INTERNAL_CALL(TransformComponent_GetTranslation);
        RF_ADD_INTERNAL_CALL(TransformComponent_SetTranslation);
        RF_ADD_INTERNAL_CALL(TransformComponent_GetRotation);
        RF_ADD_INTERNAL_CALL(TransformComponent_SetRotation);

        RF_ADD_INTERNAL_CALL(Entity_Exists);
        RF_ADD_INTERNAL_CALL(Entity_GetScript);
        RF_ADD_INTERNAL_CALL(SkeletonAnimationComponent_Move);
        RF_ADD_INTERNAL_CALL(SkeletonAnimationComponent_Face);
        RF_ADD_INTERNAL_CALL(Input_IsKeyDown);
        RF_ADD_INTERNAL_CALL(Input_IsMouseButtonDown);
        RF_ADD_INTERNAL_CALL(Input_MousePosition);
        RF_ADD_INTERNAL_CALL(Input_CaptureCursor);
    }
}
